#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e
INSTALL_DIR="dist"


echo "--- Cleaning old install ---"
rm -rf "$INSTALL_DIR"

echo "--- Configuring, Building, and Installing (Ninja) ---"
cmake --workflow --preset dev

echo "--- Done! Executable is in dist/ ---"
