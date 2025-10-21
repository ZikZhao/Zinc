#!/bin/bash

# Build script for PolyType project using CMake

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    mkdir build
fi

# Navigate to build directory
cd build

# Configure the project
echo "Configuring project with CMake..."
cmake ..

# Build the project
echo "Building project..."
make -j$(nproc)

echo "Build completed. Executable is located at: build/out/interpreter"