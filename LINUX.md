# Native Linux Build

This checkout adds a Linux/SDL3 host to upstream SpaceAnimalsRecomp 1.1.0
(`fb2fa87`). It builds a native x86-64 executable, not a Wine wrapper.
Original project: https://github.com/SerenityG4K/SpaceAnimalsRecomp.
This is an experimental downstream port, not an upstream-supported release.
Cinematic audio/animation synchronization is still under investigation.
No game-derived binaries or caches are distributed.

## Build

Requirements: an AVX2 CPU, Python 3 with capstone/lz4/zstandard, Clang,
GCC, LLD, SDL3 development files, Vulkan headers/loader, glslangValidator,
and your own extracted Star Fox 2.0.0 game data. The existing upstream
extraction format is required: `work/*.bin` and
`extracted/patch_v131072/exefs/*`.

```sh
RECOMP_DATA_ROOT=/path/to/prepared/game \
RECOMP_NUP="/path/to/your/StarFox.nup" \
JOBS=10 ./build-linux.sh
```

The input directories are reused through symlinks, without modifying their
contents. The NUP is read in place, not copied. Generated game code, objects,
shaders and executables stay in ignored output directories. A full rebuild
compiles over 900 translation units and takes a while.
The script currently requires already-prepared inputs in the upstream extraction
format; it does not yet implement a fresh Linux extraction/setup workflow.
See upstream's source-build instructions for preparing your own legal dump.

```sh
./run-linux.sh
./run-linux.sh --resolution 720 --window windowed
./run-linux.sh --precompile
```

The executable is `recomp/build/full/recomp_main`. The run script selects its
working directory so shaders, settings and logs resolve consistently.
Before play it checks/precompiles the Vulkan cache and loads shader libraries
eagerly, rather than creating them during a new scene. Progress and failures are
logged in `recomp/build/full/precompile-linux.log`. The first run after a rebuild
or driver change can take a few minutes. `RECOMP_SKIP_PRECACHE=1` bypasses the
preflight check for diagnostics; `RECOMP_SHADERS_ON_DEMAND=1` restores lazy
shader-library creation.
Saves use `$XDG_DATA_HOME/SpaceAnimalsRecomp/save`, or
`$HOME/.local/share/SpaceAnimalsRecomp/save`. `RECOMP_SAVE_DIR` overrides this.
The upstream graphical launcher is still Windows-only.

## Vulkan And Performance

- Linux uses the installed Vulkan headers and loader, ahead of the bundled
  fallback headers. The build machine has Vulkan 1.4.363 headers/loader and
  NVIDIA driver 615.78.08 on an RTX 3060.
- The renderer retains upstream's Vulkan 1.3 minimum, dynamic rendering,
  synchronization2, graphics pipeline libraries with fast linking when
  supported, asynchronous submissions, and persistent pipeline caching.
  Requesting a higher API version alone would not improve performance.
- Shader-cache enumeration now visits every matching file, rather than
  stopping after the first shader. This is necessary for full pipeline
  warm-up and correct cache signatures on Linux.
- `--precompile` uses a separate native process, monitors its memory, and
  retains upstream's driver-crash isolation and cache reuse.
- Game code uses Clang `-O2`, AVX2/x86-64-v3 and tail calls. Floating-point
  contraction stays disabled and fast-math is not enabled.
- Guest memory uses anonymous Linux mappings, populated on demand by the
  kernel. Decommitting discards old contents and makes pages inaccessible
  until recommitted. Validation uses tracked committed intervals, not repeated
  scans of `/proc/self/maps` on renderer cache misses.
- Linux audio mixes large SDL requests in bounded 4096-frame blocks.
  `RECOMP_AUDIO_LOG=1` logs both output sample rate (48,000 frames/s expected)
  and guest-stream underruns. This diagnoses starvation without changing
  the native 31.25 MHz guest counter or stretching audio.
- Closing the Linux window saves the cache and exits without racing static
  destructors against the still-running GPU worker.
- Upstream rendering optimizations remain enabled by default. For a GPU-bound
  scene, lower `render_scale` in `recomp/build/full/recomp.ini` (for example
  85 or 75), or reduce output resolution. These trade image quality for speed;
  they are not an unconditional FPS improvement.

## Limitations

This is an experimental local port, not an upstream-supported Linux release.
The Windows NGX/DLSS and FidelityFX DLL backends are unavailable; the game keeps
its own temporal anti-aliasing and optional render scaling. The Windows video
recorder, Windows heap/thread diagnostics, and Windows-specific texture/cold-page
eviction are not implemented on Linux. Linux manages paging itself instead.
Shader instructions unsupported by upstream's translator remain unsupported.

No performance uplift is claimed without a like-for-like gameplay benchmark.

## Portability Smoke Check

```sh
g++ -std=c++17 -O2 -march=x86-64-v3 \
  -fno-strict-aliasing -ffp-contract=off -Irecomp/runtime \
  recomp/verif/posix_smoke.cpp -pthread -ldl \
  -o recomp/build/full/posix_smoke
recomp/build/full/posix_smoke
```

This checks multi-file shader enumeration, guest-memory accessibility,
zero-filled recommits, memory-map queries, monotonic timing and memory reporting.
`recomp/verif/posix_audio_smoke.cpp` separately tests oversized SDL callback
requests and exact sample counts; it can be built with AddressSanitizer and
UndefinedBehaviorSanitizer and linked with SDL3.
