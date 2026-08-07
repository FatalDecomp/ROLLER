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

The core-only configuration finds SDL3 and SDL3_image. It does not find or
link WildMidi or libcdio. The pinned compatibility floor matches
`build.zig.zon`:

- SDL 3.2.22
- SDL_image 3.2.4
- CMake 3.30.2
- Ninja 1.12.0

Newer compatible SDL packages are accepted so system package security updates
do not require a source change. The Zig game build continues to use the exact
repository revisions and hashes in `build.zig.zon`.

## Dependency provisioning

SDL must be installed independently of Qt. Use dynamic development packages
that provide `SDL3Config.cmake`, `SDL3ConfigVersion.cmake`,
`SDL3_imageConfig.cmake`, and `SDL3_imageConfigVersion.cmake`.

### Windows

Install the official SDL3 and SDL3_image Visual C++ development packages for
the versions above. Add their CMake package directories to `CMAKE_PREFIX_PATH`,
or pass `SDL3_DIR` and `SDL3_image_DIR` when configuring. Keep the matching DLLs
beside the eventual editor executable.

### Linux

Install the distribution's SDL3 and SDL3_image runtime and development
packages. If they are installed under a non-system prefix, add that prefix to
`CMAKE_PREFIX_PATH`. The packages must expose the `SDL3::SDL3` and
`SDL3_image::SDL3_image` CMake targets.

### macOS

Install SDL3 and SDL3_image as dynamic packages, for example through Homebrew,
then add the package prefix reported by the package manager to
`CMAKE_PREFIX_PATH`. Framework-only installs that do not provide CMake package
configuration files are not sufficient for this build.

TrackEditor can consume the checkout as a subdirectory without enabling the
game or its dependencies:

```cmake
set(ROLLER_BUILD_GAME OFF CACHE BOOL "" FORCE)
set(ROLLER_BUILD_EDITOR_CORE ON CACHE BOOL "" FORCE)
add_subdirectory(external/ROLLER)
target_link_libraries(TrackEditor PRIVATE ROLLER::core)
```
