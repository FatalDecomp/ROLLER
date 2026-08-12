# roller-core CMake build

`roller-core` is a static library for desktop editor hosts. The default CMake
build still produces the game; consumers that only need the library configure
the repository with:

```sh
cmake -S . -B build/roller-core -G Ninja \
  -DROLLER_BUILD_GAME=OFF \
  -DROLLER_BUILD_EDITOR_CORE=ON
cmake --build build/roller-core --target roller-core
```

The core-only configuration finds SDL3 and SDL3_image. It does not find or link
WildMidi or libcdio. The pinned compatibility floor matches `build.zig.zon`:

- SDL 3.2.22
- SDL_image 3.2.4
- CMake 3.30.2
- Ninja 1.12.0

Newer compatible SDL packages are accepted so system package security updates do
not require a source change. The Zig game build continues to use the exact
repository revisions and hashes in `build.zig.zon`.

## Dependency provisioning

SDL must be installed independently of Qt. Use dynamic development packages that
provide `SDL3Config.cmake`, `SDL3ConfigVersion.cmake`, `SDL3_imageConfig.cmake`,
and `SDL3_imageConfigVersion.cmake`.

### Windows

`scripts/install-sdl-windows.ps1` downloads the official SDL3 and SDL3_image
Visual C++ development packages for the versions above, verifies their SHA-256
hashes, and prints a `CMAKE_PREFIX_PATH` for them. It is what CI uses; run it
locally the same way:

```powershell
$prefix = .\scripts\install-sdl-windows.ps1
cmake -S . -B build/roller-core -G "Visual Studio 17 2022" -A x64 `
  -DROLLER_BUILD_GAME=OFF -DROLLER_BUILD_EDITOR_CORE=ON `
  -DCMAKE_PREFIX_PATH="$prefix"
```

Its pinned versions must match `ROLLER_SDL3_MIN_VERSION` and
`ROLLER_SDL3_IMAGE_MIN_VERSION`; `tools/check_core_cmake_ci.py` fails if they
drift apart. To install by hand instead, add the packages' CMake directories to
`CMAKE_PREFIX_PATH`, or pass `SDL3_DIR` and `SDL3_image_DIR` when configuring,
and keep the matching DLLs beside the eventual editor executable.

### Linux

Install the distribution's SDL3 and SDL3_image runtime and development packages.
If they are installed under a non-system prefix, add that prefix to
`CMAKE_PREFIX_PATH`. The packages must expose the `SDL3::SDL3` and
`SDL3_image::SDL3_image` CMake targets.

### macOS

Install SDL3 and SDL3_image as dynamic packages, for example through Homebrew,
then add the package prefix reported by the package manager to
`CMAKE_PREFIX_PATH`. Framework-only installs that do not provide CMake package
configuration files are not sufficient for this build.

TrackEditor can consume the checkout as a subdirectory without enabling the game
or its dependencies:

```cmake
set(ROLLER_BUILD_GAME OFF CACHE BOOL "" FORCE)
set(ROLLER_BUILD_EDITOR_CORE ON CACHE BOOL "" FORCE)
add_subdirectory(external/ROLLER)
target_link_libraries(TrackEditor PRIVATE ROLLER::core)
```

## Continuous integration

E6-S4. The `roller-core-cmake` job in `.github/workflows/build.yml` configures
core-only, builds `roller-core`, and links and runs `editor-core-link-test` on
Linux, macOS, and Windows, on every push and pull request and nightly.

Before it existed, ROLLER CI validated the CMake *source lists* through
`tools/check_source_set_drift.py` but never compiled through CMake at all, so
the only thing that built this library was TrackEditor's CI -- and that builds
the pinned submodule commit rather than ROLLER master. A break here stayed
invisible until somebody moved the pin.

`ROLLER_WARNINGS_AS_ERRORS` is deliberately left off in that job: the legacy
sources are not warning-clean, and making them so is a separate piece of work.

## Sanitizer soak

E6-S5. The `soak-sanitizer` job in the same workflow runs two steps on Linux,
both under `-Dvalgrind`: `zig build test-editor-api`, the E0-S7 facade lifecycle
suite, and `zig build test-e1-s9-reload-soak`, E1-S9's reload soak. `-Dvalgrind`
is a build option that wraps the load/render/reload test executables in Valgrind
with `--error-exitcode=1`, so a memory error fails the step rather than being
buried in the log. It is off by default and off for pushes and pull requests,
because Valgrind is slow; `nightly-build.yml` passes `run_soak: true`.

The job lives inside the reusable build workflow on purpose. The nightly release
job depends on that whole workflow, so a memory error fails the workflow and the
release is never created. A standalone scheduled workflow would report the
failure and publish the nightly anyway.

The two steps are complementary rather than redundant: the lifecycle suite
proves each transition — init, load, render, unload, shutdown across valid,
malformed, and oversized tracks — is correct once, and the soak proves nothing
accumulates when they repeat hundreds of times against a real track. The soak
needs a track and its textures, which the repository does not carry, so the job
provisions the freeware demo assets first and passes
`-Dassets-path=zig-out/fatdata-demo -Dsoak-track=TRACK5.TRK`.

Both steps also pass **`-Dcpu=baseline`**, and that is not a tuning knob. The
build otherwise targets the runner's native CPU; `ubuntu-latest` lands on Ice
Lake Xeons, and `editor_surface.c` compiled for one emits `vpermt2ps`,
`vpcmpltud`, `vpmovm2d`, and `vcvtudq2ps` — AVX-512 that **Valgrind 3.22 cannot
decode**. It raises SIGILL inside `ed_emit_surface`, which reads like a wild
jump in ROLLER and is nothing of the kind. Baseline costs throughput the
instrumentation has already spent.

`tools/check_soak_sanitizer_ci.py` (`zig build check-soak-sanitizer-ci`) fails
if either step loses `-Dvalgrind` or `-Dcpu`, if the job stops being gated on
`run_soak`, if the nightly release stops depending on it, if a standalone
sanitizer workflow appears, or if the soak's cycle count drops below the
"hundreds" the specification asks for.
