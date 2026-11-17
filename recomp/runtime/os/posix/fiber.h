// Les fibres du jeu (nn::os) : les fibres hôte de Windows sur Windows, ucontext sur POSIX.
// Le jeu utilise ses fibres comme coroutines (la pile du fil hôte est changée à chaque
// changement de fibre) ; sous Linux, ucontext fait la même chose.
//
// ucontext ne sauvegarde que les registres callee-saved (dont les xmm) : l'état AVX (les ymm et
// les zmm) est perdu au changement de fibre, et le code recompilé en use (-march=x86-64-v3). Il
// est donc sauvegardé à part, avec xsave/xrstor, comme le fait SwitchToFiber sous Windows.
#pragma once
#ifdef _WIN32
#include <windows.h>
#else

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <ucontext.h>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

constexpr DWORD FIBER_FLAG_FLOAT_SWITCH = 0x1;
constexpr DWORD HOST_FIBER_STACK = 0;

// L'état SIMD d'une fibre (la zone xsave : 4 Ko alignés, de quoi tout l'état x86-64).
struct FiberSimd {
    alignas(64) unsigned char xsave[4096];
};

// Les états activés dans XCR0 (x87, SSE, AVX, et l'AVX-512 si le processeur l'a) : le masque
// que xsave/xrstor attendent.
inline uint64_t fiber_xcr0() {
#if defined(__x86_64__)
    uint32_t lo = 0, hi = 0;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

inline void fiber_save_simd(FiberSimd& s) {
#if defined(__x86_64__)
    _xsave64((char*)s.xsave, fiber_xcr0());
#else
    (void)s;
#endif
}
inline void fiber_restore_simd(FiberSimd& s) {
#if defined(__x86_64__)
    _xrstor64((char*)s.xsave, fiber_xcr0());
#else
    (void)s;
#endif
}

struct FIBER {
    ucontext_t ctx;
    FiberSimd simd;
    char* stack = nullptr;
    size_t stack_size = 0;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    bool finished = false;
    bool is_main = false;
};

inline FIBER*& fiber_current() {
    static thread_local FIBER* f = nullptr;
    return f;
}

// La fonction d'entrée d'une fibre : makecontext ne passe que des entiers, le pointeur de la
// fibre passe donc en deux moitiés.
inline void fiber_trampoline(uint32_t lo, uint32_t hi) {
    FIBER* f = (FIBER*)((uintptr_t)(((uint64_t)hi << 32) | lo));
    if (!f) return;
    f->entry(f->arg);
    f->finished = true;
}

// Le contexte du fil appelant : capturé une fois, au premier appel.
inline void* ConvertThreadToFiberEx(void*, DWORD) {
    static thread_local FIBER main_fiber;
    static thread_local bool captured = false;
    if (!captured) {
        getcontext(&main_fiber.ctx);
        main_fiber.is_main = true;
        captured = true;
    }
    fiber_current() = &main_fiber;
    return &main_fiber;
}
inline void* ConvertFiberToThread(void* f) {
    fiber_current() = (FIBER*)f;
    return nullptr;
}
// Vrai une fois que le fil a été converti (comme sur Windows, où le premier appel renvoie faux et
// l'appelant convertit le fil).
inline BOOL IsThreadAFiber() { return fiber_current() != nullptr; }
inline void* GetCurrentFiber() { return (void*)fiber_current(); }

inline void* CreateFiberEx(size_t stack_size, DWORD, DWORD, void (*entry)(void*), void* arg) {
    // La zone xsave doit être alignée sur 64 octets : calloc ne le garantit pas (alignof(max_align_t) = 16).
    const size_t sz = (sizeof(FIBER) + 63) & ~size_t(63);
    FIBER* f = (FIBER*)aligned_alloc(64, sz);
    if (!f) return nullptr;
    memset(f, 0, sz);
    f->stack_size = stack_size < (64u << 10) ? (64u << 10) : stack_size;
    f->stack = (char*)mmap(nullptr, f->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (f->stack == MAP_FAILED) {
        free(f);
        return nullptr;
    }
    f->entry = entry;
    f->arg = arg;
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = f->stack_size;
    f->ctx.uc_link = nullptr;
    makecontext(&f->ctx, (void (*)())fiber_trampoline, 2, (uint32_t)(uintptr_t)f, (uint32_t)((uintptr_t)f >> 32));
    return f;
}

inline void SwitchToFiber(void* target) {
    FIBER* t = (FIBER*)target;
    FIBER* prev = fiber_current();
    if (!t || !prev) return;
    fiber_save_simd(prev->simd);
    fiber_current() = t;
    swapcontext(&prev->ctx, &t->ctx);
    // revenue dans cette fibre : son état SIMD est à restaurer
    asm volatile("" ::: "memory");
    fiber_restore_simd(prev->simd);
    asm volatile("" ::: "memory");
}

inline void DeleteFiber(void* target) {
    FIBER* f = (FIBER*)target;
    if (!f || f->is_main) return;
    if (f->stack && f->stack != MAP_FAILED) munmap(f->stack, f->stack_size);
    free(f);
}

#endif  // _WIN32
