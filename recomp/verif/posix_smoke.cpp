#include "../runtime/os/posix/memory.inc"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>

namespace hos {
void logf(const char*, const char*, ...) {}
[[noreturn]] void panic(const char* message, ...) {
    fprintf(stderr, "%s\n", message);
    abort();
}
}

int main() {
    char temporary[] = "/tmp/spaceanimals-posix-XXXXXX";
    const char* directory = mkdtemp(temporary);
    assert(directory);
    const std::filesystem::path path(directory);
    std::ofstream(path / "first.spv") << "first";
    std::ofstream(path / "second.spv") << "second";
    std::ofstream(path / "ignored.meta") << "metadata";
    WIN32_FIND_DATAA entry{};
    HANDLE search = FindFirstFileA((path / "*.spv").c_str(), &entry);
    assert(search != INVALID_HANDLE_VALUE);
    std::set<std::string> names;
    do {
        names.insert(entry.cFileName);
        assert(entry.nFileSizeLow > 0);
        assert(entry.ftLastWriteTime.dwLowDateTime || entry.ftLastWriteTime.dwHighDateTime);
    } while (FindNextFileA(search, &entry));
    assert(FindClose(search));
    assert((names == std::set<std::string>{"first.spv", "second.spv"}));
    assert(FindFirstFileA((path / "*.missing").c_str(), &entry) == INVALID_HANDLE_VALUE);

    hos::mem_init();
    const uint64_t address = hos::AS_BASE + 0x10000, size = 0x2000;
    assert(!hos::mem_valid(address, size));
    assert(hos::mem_commit_lazy(address, size));
    hos::mem_set(address, size, hos::MS_NORMAL, hos::MP_RW);
    auto* bytes = reinterpret_cast<unsigned char*>(address);
    assert(bytes[0] == 0 && bytes[size - 1] == 0);
    bytes[0] = 42;
    bytes[size - 1] = 7;
    assert(hos::mem_valid(address, size));
    assert(hos::mem_check(address, size, hos::MS_NORMAL));
    assert(!hos::mem_valid(UINT64_MAX - 10, 100));
    const auto region = hos::mem_query(address);
    assert(region.addr == address && region.size == size && region.perm == hos::MP_RW);
    hos::mem_decommit(address, size);
    assert(!hos::mem_valid(address, size));
    assert(hos::mem_commit(address, size));
    assert(bytes[0] == 0 && bytes[size - 1] == 0);

    const uint64_t hole_test = address + 0x10000;
    assert(hos::mem_commit(hole_test, 0x4000));
    assert(hos::mem_valid(hole_test, 0x4000));
    hos::mem_decommit(hole_test + 0x1000, 0x2000);
    assert(hos::mem_valid(hole_test, 0x1000));
    assert(hos::mem_valid(hole_test + 0x3000, 0x1000));
    assert(!hos::mem_valid(hole_test + 0x1000, 1));
    assert(!hos::mem_valid(hole_test, 0x4000));
    assert(hos::mem_commit(hole_test + 0x1000, 0x2000));
    assert(hos::mem_valid(hole_test, 0x4000));
    hos::mem_decommit(hole_test - 0x1000, 0x6000);
    assert(!hos::mem_valid(hole_test, 0x4000));

    LARGE_INTEGER start{}, end{}, frequency{};
    assert(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
    assert(QueryPerformanceCounter(&start));
    Sleep(2);
    assert(QueryPerformanceCounter(&end) && end.QuadPart > start.QuadPart);
    MEMORYSTATUSEX memory{};
    assert(GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys > 0 && memory.ullAvailPhys > 0);

    std::filesystem::remove(path / "first.spv");
    std::filesystem::remove(path / "second.spv");
    std::filesystem::remove(path / "ignored.meta");
    std::filesystem::remove(path);
    puts("POSIX smoke checks passed (shader enumeration, guest memory, timers, memory reporting)");
}
