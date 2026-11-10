# KiRend

KiRend renders a KiCad 10 board (`.kicad_pcb`) and its 3D models as black line art on a white
background, in the style of assembly instructions. Output is SVG or PNG. The camera options are
the same as `kicad-cli pcb render`, and the image is framed exactly like kicad-cli's, so the two
can be overlaid.

```
kirend [options] INPUT_FILE
kirend -o board.svg --rotate '-45,0,45' --layers Edge.Cuts,F.Cu,F.SilkS my_board.kicad_pcb
```

Run `kirend --help` for all options. See [docs/DESIGN.md](docs/DESIGN.md) for how it works.

## Building

Requirements: CMake 3.21 or newer, Ninja (macOS/Linux), a C++20 compiler (Xcode clang, GCC 11+,
Visual Studio 2022) and [vcpkg](https://github.com/microsoft/vcpkg). vcpkg builds all
dependencies; the first build compiles OpenCASCADE and takes a while.

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg

cmake --preset macos          # or: linux, windows
cmake --build --preset macos
ctest --preset macos
```

The binary is `build/<preset>/kirend` (`build/windows/Release/kirend.exe` on Windows).

## Third-party libraries

All are fetched by vcpkg (see `vcpkg.json`, pinned with `builtin-baseline`):

| Library | Use | License |
|---|---|---|
| OpenCASCADE 8.0 | STEP and VRML reading, tessellation | LGPL 2.1 with exception |
| zstd | Embedded model decompression | BSD |
| nlohmann/json | KiCad settings and project files | MIT |
| p-ranav/argparse | Command line | MIT |
| lunasvg (with plutovg) | SVG to PNG | MIT |
| GoogleTest | Unit tests only | BSD |
