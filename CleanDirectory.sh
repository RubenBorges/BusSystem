#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

BUILD_DIR="build"
INSTALL_DIR="dist"

echo "--- Cleaning old build ---"
rm -rf $BUILD_DIR $INSTALL_DIR
