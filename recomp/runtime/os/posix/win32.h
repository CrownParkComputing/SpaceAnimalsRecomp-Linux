// Le petit sous-ensemble de l'API Win32 que la couche hôte utilise, sur POSIX (Linux).
// Le runtime est écrit pour Windows ; ce fichier en donne l'équivalent : les fenêtres, le tactile,
// les fichiers, la mémoire, les fils et les événements sont rendus par POSIX et SDL3.
//
// Les fichiers qui l'incluent gardent leur code Windows : ils font
//     #ifdef _WIN32
//     #include <windows.h>
//!    #include "posix/win32.h"
//     #endif
// ou, mieux, remplacent l'inclusion par celle-ci, qui choisit la bonne selon la plateforme.
#pragma once
// Le séparateur de chemin des fichiers (les chemins du runtime sont écrits ainsi).
#ifdef _WIN32
#define PATH_SEP "\\"
#include <windows.h>
#else
#define PATH_SEP "/"


#include <cstdarg>
#include <codecvt>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <locale>
#include <string>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ---- types -------------------------------------------------------------------------------
using BYTE = unsigned char;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = uint32_t;
using LONG = int32_t;
using ULONG = uint32_t;
using BOOL = int;
using UCHAR = unsigned char;
using USHORT = uint16_t;
using WCHAR = char16_t;
using HRESULT = int32_t;
using LRESULT = int64_t;
using WPARAM = uint64_t;
using LPARAM = int64_t;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using LPCTSTR = const char*;
using HANDLE = void*;
using HWND = void*;
using HINSTANCE = void*;
using HMODULE = void*;
using HICON = void*;
using HBRUSH = void*;
using HDC = void*;
using HMONITOR = void*;
using ULONG_PTR = uintptr_t;

struct RECT {
    LONG left = 0, top = 0, right = 0, bottom = 0;
};
struct POINT {
    LONG x = 0, y = 0;
};
struct FILETIME {
    DWORD dwLowDateTime = 0, dwHighDateTime = 0;
};
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes = 0;
    FILETIME ftCreationTime{}, ftLastAccessTime{}, ftLastWriteTime{};
    DWORD nFileSizeHigh = 0, nFileSizeLow = 0;
    char cFileName[260] = {0};
};
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes = 0;
    FILETIME ftCreationTime{}, ftLastAccessTime{}, ftLastWriteTime{};
    DWORD nFileSizeHigh = 0, nFileSizeLow = 0;
};
constexpr DWORD MOVEFILE_REPLACE_EXISTING = 0x1, MOVEFILE_COPY_ALLOWED = 0x2;

// ---- constantes --------------------------------------------------------------------------
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr HRESULT S_OK = 0, E_FAIL = 0x80004005L;
constexpr DWORD INFINITE = 0xFFFFFFFFu;
constexpr DWORD WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 0x102, WAIT_FAILED = 0xFFFFFFFFu;
constexpr DWORD WAIT_ABANDONED = 0x80;
constexpr DWORD GENERIC_READ = 0x80000000u, GENERIC_WRITE = 0x40000000u;
constexpr DWORD FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2;
constexpr DWORD CREATE_ALWAYS = 2, OPEN_EXISTING = 3, OPEN_ALWAYS = 4, TRUNCATE_EXISTING = 5;
constexpr DWORD FILE_ATTRIBUTE_NORMAL = 0x80;
constexpr DWORD FILE_FLAG_RANDOM_ACCESS = 0x80000000u, FILE_FLAG_SEQUENTIAL_SCAN = 0x08000000u;
constexpr DWORD PAGE_NOACCESS = 0x01, PAGE_READWRITE = 0x04, PAGE_EXECUTE_READWRITE = 0x40;
constexpr DWORD MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000;
constexpr DWORD TIMER_ALL_ACCESS = 0x001F0003u, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002u;
constexpr DWORD ERROR_SUCCESS = 0;
inline HANDLE const INVALID_HANDLE_VALUE = (HANDLE)(intptr_t)-1;
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress = nullptr;
    void* AllocationBase = nullptr;
    DWORD AllocationProtect = 0;
    size_t RegionSize = 0;
    DWORD State = 0;
    DWORD Protect = 0;
    DWORD Type = 0;
};
union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    long long QuadPart;
};
constexpr UINT WM_CLOSE = 0x0010, WM_DESTROY = 0x0002, WM_QUIT = 0x0012;
constexpr UINT WM_KEYDOWN = 0x0100, WM_SYSKEYDOWN = 0x0104, WM_PAINT = 0x000F, WM_ERASEBKGND = 0x0014;
constexpr UINT WM_SYSCHAR = 0x0106, WM_CHAR = 0x0102, WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201,
              WM_LBUTTONUP = 0x0202, WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205, WM_MOUSEWHEEL = 0x020A;
constexpr int VK_RETURN = 0x0D, VK_F11 = 0x7A, VK_ESCAPE = 0x1B, VK_SPACE = 0x20;
constexpr int VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28;
constexpr int VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_CAPITAL = 0x14;
constexpr int VK_LBUTTON = 0x01, VK_RBUTTON = 0x02;
constexpr int COINIT_APARTMENTTHREADED = 0x2, COINIT_MULTITHREADED = 0x0;
constexpr int GET_LAST_ERROR = 0, ERROR_FILE_NOT_FOUND = 2, ERROR_NO_MORE_FILES = 18;
constexpr int MAX_PATH = 260;
constexpr int CP_UTF8 = 65001;
constexpr int VK_BACK = 0x08, VK_TAB = 0x09;
constexpr int VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_LMENU = 0xA4,
          VK_RMENU = 0xA5;
constexpr UINT WM_MOUSEACTIVATE = 0x0021;

// ---- erreurs -----------------------------------------------------------------------------
inline DWORD GetLastError() { return errno ? (DWORD)errno : 0; }
inline void SetLastError(DWORD e) { errno = (int)e; }
inline HANDLE GetCurrentProcess() { return (HANDLE)(intptr_t)getpid(); }
inline DWORD GetCurrentThread() { return (DWORD)(uintptr_t)pthread_self(); }
inline void Sleep(DWORD ms) { struct timespec ts { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, nullptr); }
inline void ExitProcess(int code) { _exit(code); }
inline BOOL TerminateProcess(HANDLE, int code) {
    _exit(code);
    return TRUE;
}
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
struct OVERLAPPED {
    void* Internal = nullptr;
    void* InternalHigh = nullptr;
    DWORD Offset = 0, OffsetHigh = 0;
    HANDLE hEvent = nullptr;
};
struct CONTEXT {
    uint64_t Rip = 0, Rsp = 0;
    uint64_t Rbx = 0, Rbp = 0, Rsi = 0, Rdi = 0, R12 = 0, R13 = 0, R14 = 0, R15 = 0;
    DWORD ContextFlags = 0;
};
inline void* _aligned_malloc(size_t size, size_t align) {
    void* p = nullptr;
    if (align < sizeof(void*)) align = sizeof(void*);
    if (posix_memalign(&p, align, size) != 0) return nullptr;
    return p;
}
inline void _aligned_free(void* p) { free(p); }
inline uint64_t GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}
inline DWORD GetTickCount() { return GetTickCount64(); }
inline HRESULT CoInitializeEx(void*, DWORD) { return S_OK; }
inline void CoUninitialize() {}
inline void* LoadLibraryW(const wchar_t*) { return nullptr; }
inline void FreeLibrary(void*) {}
inline void* GetProcAddress(void*, const char*) { return nullptr; }
inline HMODULE GetModuleHandleW(const wchar_t*) { return (HMODULE)(uintptr_t)getpid(); }

// ---- le module où tourne le programme -----------------------------------------------------
inline DWORD GetModuleFileNameA(void*, char* buf, DWORD n) {
    ssize_t k = readlink("/proc/self/exe", buf, n ? n - 1 : 0);
    if (k <= 0) { if (n) buf[0] = 0; return 0; }
    buf[k] = 0;
    return (DWORD)k;
}

// ---- mémoire ------------------------------------------------------------------------------
// L'espace invité est réservé à une adresse fixe (0x7000000000-0x8000000000) : la réservation
// doit donc tomber exactement là (MAP_FIXED_NOREPLACE, pas une simple adresse conseillée), et
// l'engagement d'une région déjà réservée ne fait que rendre la page accessible.
inline void* VirtualAlloc(void* addr, size_t size, DWORD type, DWORD prot) {
    if (!addr || !size) return nullptr;
    if (type & MEM_RESERVE) {
        void* p = mmap(addr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) {
            SetLastError(12);   // ERROR_NOT_ENOUGH_MEMORY
            return nullptr;
        }
        return p;
    }
    // engagement : la région est déjà mappée (réservée), on lui donne ses droits
    const int p = (prot & PAGE_EXECUTE_READWRITE) ? (PROT_READ | PROT_WRITE | PROT_EXEC)
                : (prot & PAGE_NOACCESS)             ? PROT_NONE
                                                   : (PROT_READ | PROT_WRITE);
    if (mprotect(addr, size, p) != 0) {
        SetLastError(12);
        return nullptr;
    }
    return addr;
}
inline BOOL VirtualFree(void* addr, size_t, DWORD type) {
    if (type == MEM_RELEASE) return munmap(addr, 0) == 0;
    return true;
}
constexpr DWORD MEM_DECOMMIT = 0x4000, MEM_FREE = 0x10000;
constexpr DWORD PAGE_GUARD = 0x100;
constexpr DWORD STACK_SIZE_PARAM_IS_A_RESERVATION = 0x10000;
// VirtualQuery : l'état de la page qui contient l'adresse (lu dans /proc/self/maps)
inline BOOL VirtualQuery(const void* addr, MEMORY_BASIC_INFORMATION* mbi, size_t len) {
    (void)len;
    if (!mbi) return FALSE;
    memset(mbi, 0, sizeof *mbi);
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return FALSE;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        unsigned long long lo = 0, hi = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if ((uint64_t)(uintptr_t)addr < lo || (uint64_t)(uintptr_t)addr >= hi) continue;
        mbi->BaseAddress = (void*)(uintptr_t)lo;
        mbi->AllocationBase = (void*)(uintptr_t)lo;
        mbi->RegionSize = hi - lo;
        const bool readable = perms[0] == 'r', writable = perms[1] == 'w';
        mbi->State = readable || writable ? 0x1000 /* MEM_COMMIT */ : MEM_RESERVE;
        mbi->Protect = writable ? PAGE_READWRITE : readable ? 0x02 /* PAGE_READONLY */ : PAGE_NOACCESS;
        mbi->Type = 0x20000;   // MEM_PRIVATE
        fclose(f);
        return TRUE;
    }
    fclose(f);
    return FALSE;
}

// ---- fenêtres (SDL3) -----------------------------------------------------------------------
// La fenêtre hôte est créée par present.cpp (SDL3). Ici, seulement ce dont le moteur Vulkan a
// besoin : la taille de la zone client.
namespace hos {
void* gfx_window_handle();
bool gfx_client_rect(int* w, int* h);
}   // namespace hos
inline BOOL GetClientRect(HWND h, RECT* r) {
    int w = 0, h2 = 0;
    if (!hos::gfx_client_rect(&w, &h2)) { r->right = r->bottom = 0; return FALSE; }
    r->left = r->top = 0;
    r->right = w;
    r->bottom = h2;
    return TRUE;
}

// ---- fichiers -----------------------------------------------------------------------------
inline HANDLE CreateFileA(const char* path, DWORD access, DWORD share, void*, DWORD creation, DWORD, void*) {
    int flags = (access & GENERIC_WRITE) ? O_RDWR : O_RDONLY;
    if (creation == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    else if (creation == OPEN_ALWAYS) flags |= O_CREAT;
    int fd = open(path, flags, 0644);
    return fd < 0 ? (HANDLE)(intptr_t)-1 : (HANDLE)(intptr_t)fd;
}
inline HANDLE CreateFileW(const wchar_t* w, DWORD a, DWORD s, void*, DWORD c, DWORD f, void*) {
    (void)w; (void)a; (void)s; (void)c; (void)f;
    return (HANDLE)(intptr_t)-1;
}
inline BOOL ReadFile(HANDLE h, void* buf, DWORD n, DWORD* got, OVERLAPPED* ov) {
    // Un OVERLAPPED avec un décalage : la lecture se fait à cette position du fichier (le .nup
    // du jeu fait 30 Go, ses entrées sont à des décalages 64 bits).
    if (ov && (ov->Offset || ov->OffsetHigh)) {
        const int64_t off = (int64_t)(((uint64_t)ov->OffsetHigh << 32) | ov->Offset);
        if (lseek((int)(intptr_t)h, (off_t)off, SEEK_SET) < 0) {
            if (got) *got = 0;
            return FALSE;
        }
    }
    ssize_t k = ::read((int)(intptr_t)h, buf, n);
    if (got) *got = k > 0 ? (DWORD)k : 0;
    return k >= 0;
}
inline BOOL CloseHandle(HANDLE h) {
    if (!h || h == (HANDLE)(intptr_t)-1) return FALSE;
    return close((int)(intptr_t)h) == 0;
}
inline BOOL DeleteFileA(const char* p) { return unlink(p) == 0; }
inline BOOL DeleteFileW(const wchar_t* p) { (void)p; return FALSE; }
inline BOOL MoveFileA(const char* a, const char* b) { return rename(a, b) == 0; }
inline BOOL MoveFileExA(const char* a, const char* b, DWORD) { return rename(a, b) == 0; }
inline BOOL MoveFileW(const wchar_t* a, const wchar_t* b) { (void)a; (void)b; return FALSE; }
inline BOOL CreateDirectoryA(const char* p, void*) { return mkdir(p, 0755) == 0 || errno == EEXIST; }
struct PosixFind {
    DIR* stream;
    std::string directory, pattern;
};
inline BOOL FindNextFileA(HANDLE handle, WIN32_FIND_DATAA* data) {
    auto* find = static_cast<PosixFind*>(handle);
    while (dirent* entry = readdir(find->stream)) {
        if (fnmatch(find->pattern.c_str(), entry->d_name, 0)) continue;
        struct stat info{};
        if (stat((find->directory + "/" + entry->d_name).c_str(), &info)) continue;
        *data = {};
        data->dwFileAttributes = S_ISDIR(info.st_mode) ? 0x10 : FILE_ATTRIBUTE_NORMAL;
        snprintf(data->cFileName, sizeof data->cFileName, "%s", entry->d_name);
        data->nFileSizeLow = (DWORD)info.st_size;
        data->nFileSizeHigh = (DWORD)((uint64_t)info.st_size >> 32);
        const uint64_t stamp = (uint64_t)info.st_mtim.tv_sec * 10000000 + info.st_mtim.tv_nsec / 100;
        data->ftLastWriteTime = {(DWORD)stamp, (DWORD)(stamp >> 32)};
        return TRUE;
    }
    return FALSE;
}
inline BOOL FindClose(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
    auto* find = static_cast<PosixFind*>(handle);
    closedir(find->stream);
    delete find;
    return TRUE;
}
inline HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data) {
    const std::string path(pattern);
    const auto slash = path.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
    DIR* stream = opendir(directory.c_str());
    if (!stream) return INVALID_HANDLE_VALUE;
    auto* find = new PosixFind{stream, directory, path.substr(slash == std::string::npos ? 0 : slash + 1)};
    if (FindNextFileA(find, data)) return find;
    FindClose(find);
    return INVALID_HANDLE_VALUE;
}
constexpr int GetFileExInfoStandard = 0;   // le niveau de GetFileAttributesEx
inline BOOL GetFileAttributesExA(const char* path, int, WIN32_FILE_ATTRIBUTE_DATA* d) {
    struct stat st;
    if (!d || !path || stat(path, &st) != 0) return FALSE;
    d->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeLow = (DWORD)st.st_size;
    d->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    const uint64_t ft = (uint64_t)st.st_mtime * 10000000ull;
    d->ftLastWriteTime.dwLowDateTime = (DWORD)ft;
    d->ftLastWriteTime.dwHighDateTime = (DWORD)(ft >> 32);
    return TRUE;
}
constexpr DWORD INVALID_FILE_ATTRIBUTES = 0xFFFFFFFFu;
inline DWORD GetFileAttributesA(const char* path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return FILE_ATTRIBUTE_NORMAL;
}
inline int _mkdir(const char* p) { return mkdir(p, 0755); }
inline int _access(const char* p, int mode) { return access(p, mode); }

// La mémoire du processus (le journal y dit la mémoire privée du jeu)
struct PROCESS_MEMORY_COUNTERS {
    DWORD cb = 0, PageFaultCount = 0;
    size_t PeakWorkingSetSize = 0, WorkingSetSize = 0;
    size_t QuotaPeakPagedPoolUsage = 0, QuotaPagedPoolUsage = 0;
    size_t QuotaPeakNonPagedPoolUsage = 0, QuotaNonPagedPoolUsage = 0;
    size_t PagefileUsage = 0, PeakPagefileUsage = 0;
    size_t PrivateUsage = 0;
};
struct PROCESS_MEMORY_COUNTERS_EX : PROCESS_MEMORY_COUNTERS {};
inline BOOL K32GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS* c, DWORD) {
    if (!c) return FALSE;
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return FALSE;
    long total = 0, rss = 0;
    const int got = fscanf(f, "%ld %ld", &total, &rss);
    fclose(f);
    if (got != 2) return FALSE;
    const long page = sysconf(_SC_PAGESIZE);
    c->WorkingSetSize = (size_t)rss * (size_t)page;
    c->PrivateUsage = (size_t)total * (size_t)page;
    return TRUE;
}

// ---- fils, événements ---------------------------------------------------------------------
// Objets du noyau : un « handle » est un petit objet mallocé, freed par CloseHandle.
struct PosixEvent {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    bool signalled = false;
    bool manual = true;
    int64_t deadline_us = -1;   // -1 : pas d'échéance (un simple événement)
};
struct PosixThread {
    pthread_t t{};
    std::string name;
};
inline HANDLE CreateEventA(void*, BOOL manual, BOOL initial, const char*) {
    PosixEvent* e = new PosixEvent();
    e->manual = manual != 0;
    e->signalled = initial != 0;
    return (HANDLE)e;
}
inline HANDLE CreateEventW(void*, BOOL manual, BOOL initial, const wchar_t*) {
    return CreateEventA(nullptr, manual, initial, nullptr);
}
inline BOOL SetEvent(HANDLE h) {
    PosixEvent* e = (PosixEvent*)h;
    if (!e) return FALSE;
    pthread_mutex_lock(&e->m);
    e->signalled = true;
    pthread_cond_broadcast(&e->c);
    pthread_mutex_unlock(&e->m);
    return TRUE;
}
inline BOOL ResetEvent(HANDLE h) {
    PosixEvent* e = (PosixEvent*)h;
    if (!e) return FALSE;
    pthread_mutex_lock(&e->m);
    e->signalled = false;
    pthread_mutex_unlock(&e->m);
    return TRUE;
}
inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    PosixEvent* e = (PosixEvent*)h;
    if (!e) return WAIT_FAILED;
    pthread_mutex_lock(&e->m);
    while (!e->signalled) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        int64_t deadline_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
        if (ms != INFINITE) deadline_us += ms * 1000;
        if (e->deadline_us >= 0 && e->deadline_us < deadline_us) deadline_us = e->deadline_us;
        ts.tv_sec = deadline_us / 1000000;
        ts.tv_nsec = (long)(deadline_us % 1000000) * 1000;
        if (ms == INFINITE && e->deadline_us < 0) pthread_cond_wait(&e->c, &e->m);
        else {
            int r = pthread_cond_timedwait(&e->c, &e->m, &ts);
            if (r == ETIMEDOUT) {
                if (e->deadline_us >= 0 && e->deadline_us <= deadline_us) { e->signalled = true; break; }
                pthread_mutex_unlock(&e->m);
                return WAIT_TIMEOUT;
            }
        }
    }
    if (!e->manual) e->signalled = false;   // événement auto-reset
    pthread_mutex_unlock(&e->m);
    return WAIT_OBJECT_0;
}

// Un minuteur : l'échéance est portée par l'événement lui-même (le compte à rebours est en
// centièmes de microseconde, négatif pour un délai relatif, comme sous Windows).
inline HANDLE CreateWaitableTimerExW(void*, void*, DWORD, DWORD) { return CreateEventA(nullptr, TRUE, FALSE, nullptr); }
inline BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER* due, DWORD, void*, void*, BOOL) {
    PosixEvent* e = (PosixEvent*)h;
    if (!e || !due) return FALSE;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t now_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
    int64_t delay_us = -due->QuadPart / 10;   // 100 ns → µs
    pthread_mutex_lock(&e->m);
    e->deadline_us = now_us + delay_us;
    e->signalled = false;
    pthread_mutex_unlock(&e->m);
    return TRUE;
}
inline DWORD WaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL all, DWORD ms) {
    // Attendre plusieurs objets : le premier prêt (ou tous si all). L'index du premier prêt est renvoyé
    // (WAIT_OBJECT_0 + index) ; un minuteur dont l'échéance est passée compte comme prêt.
    const uint64_t start = GetTickCount64();
    const uint64_t deadline = ms == INFINITE ? 0 : start + ms;
    for (;;) {
        DWORD ready_count = 0, ready_index = 0;
        bool found = false;
        for (DWORD i = 0; i < n; i++) {
            PosixEvent* e = (PosixEvent*)hs[i];
            if (!e) continue;
            pthread_mutex_lock(&e->m);
            bool s = e->signalled;
            if (!s && e->deadline_us >= 0) {   // minuteur : l'échéance est passée
                struct timespec now;
                clock_gettime(CLOCK_REALTIME, &now);
                const int64_t now_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
                if (e->deadline_us <= now_us) {
                    s = true;
                    e->signalled = true;
                }
            }
            if (s && !e->manual) e->signalled = false;   // événement à remise à zéro automatique
            pthread_mutex_unlock(&e->m);
            if (s) {
                ready_count++;
                if (!found) {
                    ready_index = i;
                    found = true;
                }
            }
        }
        if (found && (!all || ready_count == n)) return WAIT_OBJECT_0 + ready_index;
        if (deadline) {
            const uint64_t now = GetTickCount64();
            if (now >= deadline) return WAIT_TIMEOUT;
        }
        // on attend un peu sur le premier objet : son réveil nous fait réexaminer les autres
        PosixEvent* w = n ? (PosixEvent*)hs[0] : nullptr;
        if (w) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 1000000L;   // 1 ms
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_mutex_lock(&w->m);
            if (!w->signalled) pthread_cond_timedwait(&w->c, &w->m, &ts);
            pthread_mutex_unlock(&w->m);
        } else {
            Sleep(1);
        }
    }
}
typedef unsigned (*LPTHREAD_START_ROUTINE)(void*);
// Les fils du noyau : comme sur Windows, la fonction d'entrée rend la main et le fil se termine.
// Le handle du fil est un objet d'événement : WaitForSingleObject(y) attend la fin du fil.
struct PosixThreadStart {
    LPTHREAD_START_ROUTINE fn;
    void* arg;
    PosixEvent* done;
};
inline void* posix_thread_entry(void* p) {
    PosixThreadStart* st = (PosixThreadStart*)p;
    LPTHREAD_START_ROUTINE fn = st->fn;
    void* arg = st->arg;
    PosixEvent* done = st->done;
    delete st;
    fn(arg);
    if (done) {   // le fil est terminé : ceux qui l'attendent peuvent repartir
        pthread_mutex_lock(&done->m);
        done->signalled = true;
        pthread_cond_broadcast(&done->c);
        pthread_mutex_unlock(&done->m);
    }
    return nullptr;
}
inline uintptr_t _beginthreadex(void*, unsigned, LPTHREAD_START_ROUTINE fn, void* arg, unsigned, unsigned*) {
    PosixEvent* done = new PosixEvent();
    PosixThreadStart* st = new PosixThreadStart{fn, arg, done};
    pthread_t t;
    if (pthread_create(&t, nullptr, posix_thread_entry, st) != 0) {
        delete st;
        delete done;
        return 0;
    }
    pthread_detach(t);
    return (uintptr_t)done;
}
inline void _endthreadex(void*) {}
inline BOOL SwitchToThread() {
    sched_yield();
    return TRUE;
}
typedef void* (*LPTHREAD_START_ROUTINE_WIN)(void*);
inline uintptr_t CreateThread(void* attrs, DWORD stack, LPTHREAD_START_ROUTINE_WIN fn, void* param, DWORD flags, DWORD* id) {
    (void)attrs; (void)stack; (void)flags;
    pthread_t t;
    if (pthread_create(&t, nullptr, fn, param) != 0) return 0;
    pthread_detach(t);
    if (id) *id = (DWORD)(uintptr_t)t;
    return (uintptr_t)t;
}
inline DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL) { return WaitForSingleObject(h, ms); }

inline int _fseeki64(FILE* f, int64_t off, int whence) { return fseeko(f, off, whence); }
inline int64_t _ftelli64(FILE* f) { return ftello(f); }
inline void YieldProcessor() { sched_yield(); }

// Les fonctions de date et heure « sûres » de Microsoft, avec l'ordre des arguments de POSIX.
inline int localtime_s(struct tm* tm, const time_t* t) { return localtime_r(t, tm) ? 0 : EINVAL; }
inline int gmtime_s(struct tm* tm, const time_t* t) { return gmtime_r(t, tm) ? 0 : EINVAL; }
inline time_t _mkgmtime(const struct tm* tm) { return timegm((struct tm*)tm); }

// ---- la base de l'image qui contient une adresse (diagnostic : symbolized) ------------------
inline uint64_t host_module_base(uint64_t addr) {
#ifdef _WIN32
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)addr, &m);
    return (uint64_t)(uintptr_t)m;
#else
    // /proc/self/maps : la carte qui contient l'adresse, puis la plus basse carte du même fichier
    // (la base de l'image, à laquelle les adresses virtuelles du fichier correspondent).
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return addr;
    char line[1024];
    char file[512] = {0};
    uint64_t base = addr, lowest = addr;
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        unsigned long long lo = 0, hi = 0;
        if (sscanf(line, "%llx-%llx", &lo, &hi) != 2) continue;
        char* slash = strrchr(line, '/');
        if (!found && addr >= lo && addr < hi) {
            base = lo;
            found = true;
            if (slash) snprintf(file, sizeof file, "%s", slash);
        }
    }
    fclose(f);
    if (!found) return addr;
    // la base de l'image : la plus basse carte du même fichier
    f = fopen("/proc/self/maps", "r");
    if (!f) return base;
    while (fgets(line, sizeof line, f)) {
        unsigned long long lo = 0, hi = 0;
        if (sscanf(line, "%llx-%llx", &lo, &hi) != 2) continue;
        char* slash = strrchr(line, '/');
        if (slash && !strcmp(slash, file) && lo < lowest) lowest = lo;
    }
    fclose(f);
    return lowest;
#endif
}

struct MEMORYSTATUSEX {
    DWORD dwLength = sizeof(MEMORYSTATUSEX), dwMemoryLoad = 0;
    uint64_t ullTotalPhys = 0, ullAvailPhys = 0, ullTotalPageFile = 0, ullAvailPageFile = 0;
    uint64_t ullTotalVirtual = 0, ullAvailVirtual = 0, ullAvailExtendedVirtual = 0;
};
inline BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* status) {
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) return FALSE;
    char line[256], key[64];
    unsigned long long value = 0;
    uint64_t swap = 0, swap_free = 0;
    while (fgets(line, sizeof line, file)) {
        if (sscanf(line, "%63s %llu", key, &value) != 2) continue;
        const uint64_t bytes = value * 1024;
        if (!strcmp(key, "MemTotal:")) status->ullTotalPhys = bytes;
        if (!strcmp(key, "MemAvailable:")) status->ullAvailPhys = bytes;
        if (!strcmp(key, "SwapTotal:")) swap = bytes;
        if (!strcmp(key, "SwapFree:")) swap_free = bytes;
    }
    fclose(file);
    status->ullTotalPageFile = status->ullTotalPhys + swap;
    status->ullAvailPageFile = status->ullAvailPhys + swap_free;
    status->dwMemoryLoad = status->ullTotalPhys ? 100 - status->ullAvailPhys * 100 / status->ullTotalPhys : 0;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) { frequency->QuadPart = 1000000000; return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* counter) {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return FALSE;
    counter->QuadPart = now.tv_sec * 1000000000ll + now.tv_nsec;
    return TRUE;
}
inline size_t _msize(void* p) { return malloc_usable_size(p); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _putenv_s(const char* name, const char* value) { return setenv(name, value, 1); }
inline std::string posix_utf8(const std::wstring& value) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(value);
}
inline std::wstring posix_wide(const std::string& value) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(value);
}
inline int WideCharToMultiByte(unsigned, DWORD, const wchar_t* input, int size, char* output, int capacity, void*, void*) {
    const std::string text = posix_utf8(size < 0 ? std::wstring(input) : std::wstring(input, size));
    const size_t bytes = text.size() + (size < 0);
    if (!output) return bytes;
    if (capacity < 0 || (size_t)capacity < bytes) return 0;
    memcpy(output, text.c_str(), bytes);
    return bytes;
}
inline const wchar_t* _wgetenv(const wchar_t* name) {
    thread_local std::wstring value;
    std::string key = posix_utf8(name);
    if (key == "USERPROFILE") key = "HOME";
    const char* text = getenv(key.c_str());
    if (!text) return nullptr;
    value = posix_wide(text);
    return value.c_str();
}
inline DWORD GetModuleFileNameW(void* module, wchar_t* output, DWORD capacity) {
    char path[4096];
    if (!GetModuleFileNameA(module, path, sizeof path)) return 0;
    const auto wide = posix_wide(path);
    if (wide.size() >= capacity) return 0;
    std::wmemcpy(output, wide.c_str(), wide.size() + 1);
    return wide.size();
}
inline DWORD GetFileAttributesW(const wchar_t* path) { return GetFileAttributesA(posix_utf8(path).c_str()); }
inline FILE* _wfopen(const char* path, const wchar_t* mode) { return fopen(path, posix_utf8(mode).c_str()); }

#include "fiber.h"   // les fibres du jeu (ucontext) : après les types ci-dessus

#endif  // _WIN32
