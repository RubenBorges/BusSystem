#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e
# 1. Define paths
BUILD_DIR="build"
INSTALL_DIR="dist"

echo "--- Cleaning old build ---"
rm -rf $BUILD_DIR $INSTALL_DIR

echo "--- Configuring ---"
cmake --workflow --preset dev

echo "--- Building ---"
# -j $(nproc) uses all your CPU cores to speed up compilation
cmake --build $BUILD_DIR -j $(nproc)

echo "--- Installing to $INSTALL_DIR ---"
cmake --install $BUILD_DIR

echo "--- Done! Executable is in $INSTALL_DIR/bin/ ---"