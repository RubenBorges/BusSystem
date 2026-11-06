#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# 1. Define paths
BUILD_DIR="build"
INSTALL_DIR="dist"

echo "--- Cleaning old build ---"
rm -rf $BUILD_DIR $INSTALL_DIR

echo "--- Configuring ---"
# -B specifies build directory, -DCMAKE_INSTALL_PREFIX sets the dist folder
cmake -B $BUILD_DIR -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR

echo "--- Building ---"
# -j $(nproc) uses all your CPU cores to speed up compilation
cmake --build $BUILD_DIR -j $(nproc)

echo "--- Installing to $INSTALL_DIR ---"
cmake --install $BUILD_DIR

echo "--- Done! Executable is in $INSTALL_DIR/bin/ ---"