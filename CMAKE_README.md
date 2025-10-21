# CMake Build Instructions

This project has been migrated from Makefile to CMake. Here are the build instructions:

## Prerequisites

Make sure you have the following tools installed:
- CMake (version 3.20 or higher)
- g++ compiler with C++23 support
- Bison (for parser generation)
- Flex (for lexer generation)
- Make (for building)

## Building the Project

### Option 1: Using the build script (Recommended)
```bash
./build.sh
```

### Option 2: Manual CMake build
```bash
# Create and enter build directory
mkdir build
cd build

# Configure the project
cmake ..

# Build the project
make -j$(nproc)
```

### Option 3: Using CMake directly (out-of-source build)
```bash
# From project root directory
cmake -B build -S .
cmake --build build -j$(nproc)
```

## Cleaning the Build

### Using the clean script
```bash
./clean.sh
```

### Manual cleanup
```bash
rm -rf build out
```

## Running the Executable

After building, the executable will be located at:
```
build/out/interpreter
```

You can run it with:
```bash
./build/out/interpreter
```

## Build Configuration

The CMakeLists.txt is configured with:
- Debug build by default (with sanitizers enabled)
- C++23 standard
- All the same compiler flags as the original Makefile
- Automatic generation of parser and lexer files
- Precompiled headers support

## Differences from Makefile

1. **Build directory**: CMake uses an out-of-source build in the `build/` directory
2. **Generated files**: Parser and lexer files are generated in `build/out/`
3. **Executable location**: The final executable is in `build/out/interpreter`
4. **Dependency tracking**: CMake automatically handles dependencies better than Make
5. **Cross-platform**: CMake can generate build files for different build systems

## Notes

- The precompiled header (`pch.hpp`) is automatically handled by CMake
- All sanitizer flags and debug options from the original Makefile are preserved
- The clean operation removes the entire build directory