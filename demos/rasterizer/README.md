# Rasterizer Demo (Zinc Port)

This folder contains the Zinc-language port of the rasterizer pipeline from the native C++ project:

- https://github.com/ZikZhao/Computer-Graphics-2025

## What This Demo Implements

The current Zinc port focuses on the rasterization path only:

- CPU wireframe rendering
- CPU triangle rasterization
- Sutherland-Hodgman frustum clipping in clip space
- Perspective-correct UV interpolation
- Z-buffer depth testing
- Real-time camera controls and interactive mode switching

## Scope and Current Limitation

This is a transcription of the original native C++ renderer, but only the rasterizer part is implemented in this repository.

The raytracer/path-tracing features from the original C++ project are not implemented yet in this Zinc version, because that part depends on compile-time-heavy machinery that is still under development in Zinc.

You can see this directly in runtime controls:

- `1`: Wireframe mode (implemented)
- `2`: Rasterized mode (implemented)
- `3`: Raytraced mode (not supported in this demo)
- `4`: Depth of Field mode (not supported in this demo)
- `0`: Photon visualization mode (not supported in this demo)

## Environment Requirements

Recommended platform: Linux x86-64.

To build everything from source (compiler + demo), prepare:

- Latest `g++` with C++23 support
- `cmake` (3.24+ recommended)
- `make`
- `pkg-config`
- Java (JDK or JRE, used by ANTLR in compiler build flow)
- Python 3 (helper scripts)
- SDL2 development package (`libsdl2-dev` on Debian/Ubuntu)

Typical Ubuntu/Debian bootstrap:

```bash
sudo apt update
sudo apt install -y build-essential g++ cmake make pkg-config libsdl2-dev default-jre python3
```

Notes:

- GLM headers are already vendored under `demos/rasterizer/libs/`.
- If your `g++` is old, upgrade to a newer GCC release first.

## Quick Run (Using Existing Generated C++)

If `main.zn.cpp` is already present (it is in this folder), you can compile and run directly:

```bash
cd demos/rasterizer
make
./main model/cornell-box/scene.txt
```

You can also try:

```bash
./main model/metal-gallery/scene.txt
./main model/transparency-gallery/scene.txt
./main model/light-gallery/scene.txt
```

## Re-generate C++ from Zinc Source

From repository root:

```bash
# 1) Build Zinc compiler
cmake --preset release
cmake --build --preset release --target zinc

# 2) Transpile rasterizer entry
./build/debug/bin/zinc demos/rasterizer/main.zn

# 3) Compile and run demo
cd demos/rasterizer
make clean
make
./main model/cornell-box/scene.txt
```

## Controls

- Movement: `W/A/S/D`, `Space`, `C`
- Roll: `Q/E`
- Mouse look: hold left mouse button and drag
- Keyboard look: arrow keys
- Toggle orbit: `O`
- Save screenshot: `Ctrl+S`

## If You Cannot Set Up the Toolchain

If local environment setup fails, use the prebuilt Linux binary of Zinc:

1. Open this repository's Releases page.
2. Find the Actions-produced artifact/asset named similar to `zinc-linux-x86-64`.
3. Download it and make it executable:

```bash
chmod +x ./zinc-linux-x86-64
```

4. Use it to transpile, then compile the demo:

```bash
./zinc-linux-x86-64 demos/rasterizer/main.zn
cd demos/rasterizer
make
./main model/cornell-box/scene.txt
```
