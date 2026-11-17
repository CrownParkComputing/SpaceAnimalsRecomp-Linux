#include "../hos.h"
#include "win32.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>

namespace hos {
namespace {
struct Region {
    uint64_t end;
    uint32_t state, perm, attr;
};
std::map<uint64_t, Region> regions;
std::recursive_mutex region_lock;
std::atomic<uint64_t> generation{1};
std::map<uint64_t, uint64_t> committed;
std::shared_mutex commit_lock;

void split_region(uint64_t address) {
    if (address <= AS_BASE || address >= AS_END) return;
    auto it = std::prev(regions.upper_bound(address));
    if (it->first == address) return;
    Region next = it->second;
    it->second.end = address;
    regions.emplace(address, next);
}

template <typename F>
void update_regions(uint64_t address, uint64_t size, F update) {
    if (!size || address < AS_BASE || address >= AS_END || size > AS_END - address) return;
    const uint64_t end = address + size;
    split_region(address);
    split_region(end);
    for (auto it = regions.lower_bound(address); it != regions.end() && it->first < end; ++it)
        update(it->second);
    for (auto it = regions.begin(); it != regions.end();) {
        auto next = std::next(it);
        if (next == regions.end()) break;
        const Region& a = it->second;
        const Region& b = next->second;
        if (a.end == next->first && a.state == b.state && a.perm == b.perm && a.attr == b.attr) {
            it->second.end = b.end;
            regions.erase(next);
        } else {
            it = next;
        }
    }
}
}

void mem_init() {
    void* result = mmap((void*)AS_BASE, AS_END - AS_BASE, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
    if (result == MAP_FAILED)
        panic("cannot reserve guest address space: %s", strerror(errno));
    regions.emplace(AS_BASE, Region{AS_END, MS_FREE, MP_NONE, 0});
    HLOG("mem", "Linux anonymous guest memory: demand-paged by the kernel; Windows page reclamation disabled");
}

bool mem_commit(uint64_t address, uint64_t size) {
    if (!size) return true;
    if (address < AS_BASE || address >= AS_END || size > AS_END - address) return false;
    std::unique_lock<std::shared_mutex> guard(commit_lock);
    if (mprotect((void*)address, size, PROT_READ | PROT_WRITE)) return false;
    uint64_t start = address, end = address + size;
    auto it = committed.lower_bound(start);
    if (it != committed.begin() && std::prev(it)->second >= start) --it;
    while (it != committed.end() && it->first <= end) {
        start = std::min(start, it->first);
        end = std::max(end, it->second);
        it = committed.erase(it);
    }
    committed.emplace(start, end);
    return true;
}

// Anonymous Linux mappings already fault in zero-filled pages on first access.
bool mem_commit_lazy(uint64_t address, uint64_t size) { return mem_commit(address, size); }
void mem_decommit(uint64_t address, uint64_t size) {
    if (!size) return;
    if (address < AS_BASE || address >= AS_END || size > AS_END - address)
        panic("invalid guest memory decommit");
    std::unique_lock<std::shared_mutex> guard(commit_lock);
    if (mprotect((void*)address, size, PROT_NONE) || madvise((void*)address, size, MADV_DONTNEED))
        panic("guest memory decommit failed: %s", strerror(errno));
    const uint64_t end = address + size;
    auto it = committed.upper_bound(address);
    if (it != committed.begin()) --it;
    while (it != committed.end() && it->first < end) {
        const uint64_t start = it->first, limit = it->second;
        if (limit <= address) { ++it; continue; }
        it = committed.erase(it);
        if (start < address) committed.emplace(start, address);
        if (limit > end) { committed.emplace(end, limit); break; }
    }
    generation.fetch_add(1, std::memory_order_release);
}

void mem_set(uint64_t address, uint64_t size, uint32_t state, uint32_t perm, uint32_t attr) {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    update_regions(address, size, [&](Region& r) { r.state = state; r.perm = perm; r.attr = attr; });
}
void mem_set_perm(uint64_t address, uint64_t size, uint32_t perm) {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    update_regions(address, size, [&](Region& r) { r.perm = perm; });
}
void mem_set_attr(uint64_t address, uint64_t size, uint32_t mask, uint32_t attr) {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    update_regions(address, size, [&](Region& r) { r.attr = (r.attr & ~mask) | (attr & mask); });
}
MemInfo mem_query(uint64_t address) {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    MemInfo result{};
    if (address < AS_BASE || address >= AS_END) {
        result.addr = address < AS_BASE ? 0 : AS_END;
        result.size = address < AS_BASE ? AS_BASE : 0 - AS_END;
        result.state = MS_INACCESSIBLE;
        return result;
    }
    auto it = std::prev(regions.upper_bound(address));
    result.addr = it->first;
    result.size = it->second.end - it->first;
    result.state = it->second.state;
    result.perm = it->second.perm;
    result.attr = it->second.attr;
    return result;
}
bool mem_check(uint64_t address, uint64_t size, uint32_t state) {
    if (address < AS_BASE || address >= AS_END || size > AS_END - address) return false;
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    for (uint64_t a = address; a < address + size;) {
        auto it = std::prev(regions.upper_bound(a));
        if (it->second.state != state) return false;
        a = it->second.end;
    }
    return true;
}
uint64_t mem_find_free(uint64_t base, uint64_t end, uint64_t size, uint64_t align) {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    for (const auto& [address, region] : regions) {
        if (region.state != MS_FREE) continue;
        const uint64_t start = (std::max(address, base) + align - 1) & ~(align - 1);
        const uint64_t limit = std::min(region.end, end);
        if (start < limit && size <= limit - start) return start;
    }
    return 0;
}
bool mem_valid(uint64_t address, uint64_t size) {
    if (address < AS_BASE || address >= AS_END || size > AS_END - address) return false;
    struct Cached { uint64_t gen = 0, start = 0, end = 0; };
    thread_local Cached cache[8];
    thread_local unsigned next = 0;
    const uint64_t gen = generation.load(std::memory_order_acquire), end = address + size;
    for (const auto& c : cache)
        if (c.gen == gen && address >= c.start && end <= c.end) return true;
    // All guest protection changes pass through this backend. Avoid parsing
    // /proc/self/maps on renderer cache misses (thousands of checks per frame).
    std::shared_lock<std::shared_mutex> guard(commit_lock);
    auto it = committed.upper_bound(address);
    if (it == committed.begin()) return false;
    --it;
    if (address >= it->second || end > it->second) return false;
    cache[next++ % 8] = {gen, it->first, it->second};
    return true;
}
uint64_t mem_used() {
    std::lock_guard<std::recursive_mutex> guard(region_lock);
    uint64_t total = 0;
    for (const auto& [address, region] : regions)
        if (region.state != MS_FREE && region.state != MS_INACCESSIBLE) total += region.end - address;
    return total;
}

// The Windows texture eviction machinery suspends threads and restores file-backed
// pages in its exception handler. Never enable that protocol on a POSIX signal path.
void mem_release_init() {}
bool mem_release_actif() { return false; }
uint64_t mem_release(uint64_t, uint64_t, uint64_t* id) { if (id) *id = 0; return 0; }
bool mem_released_intact(uint64_t, uint64_t, uint64_t) { return false; }
void mem_restore(uint64_t, uint64_t) {}
void mem_release_vivant(uint64_t, uint64_t, uint64_t, bool) {}
void mem_release_stats(RelStats* stats) { *stats = {}; }
bool mem_ensure(uint64_t address, uint64_t size) { return mem_valid(address, size); }
int mem_fault(uint64_t, bool) { return 0; }
void mem_lazy_stats(uint64_t* blocks, uint64_t* bytes, uint64_t* reserved) {
    *blocks = 0; *bytes = mem_used(); *reserved = AS_END - AS_BASE;
}
void mem_marge_init() {}
int mem_pression() { return 0; }
void mem_veille_init() {}
void mem_census_init() {}
void mem_vides_init() {}
void mem_noyau_debut() {}
void mem_noyau_fin() {}
void mem_epingler(uint64_t, uint64_t) {}
void mem_desepingler(uint64_t, uint64_t) {}
}
