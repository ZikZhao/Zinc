#!/bin/bash

# Clean script for PolyType project using CMake

echo "Cleaning build artifacts..."

# Remove build directory
if [ -d "build" ]; then
    rm -rf build
    echo "Removed build directory"
fi

# Remove any out directory that might exist in the source directory
if [ -d "out" ]; then
    rm -rf out
    echo "Removed out directory"
fi

echo "Clean completed."