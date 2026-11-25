#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Default values
PRESET="release"
CLEAN=false
PROJECTNAME="distributedBusSystem"
TIDYUP=false

# Help function
usage() {
    echo "Usage: $0 [-p preset] [-c] [-P projectName] [-t] [-h]"
    echo "  -p  Workflow preset: dev, release, win [default: release]"
    echo "  -c  Clean build and install directories before building"
    echo "  -P  Set project name [default: 'distributedBusSystem']"
    echo "  -t  Tidy up build directory after installation is complete"
    echo "  -h  Show this help message"
    exit 1
}

# Parse command-line arguments
while getopts "p:cP:th" opt; do
    case ${opt} in
        p)
            case "${OPTARG,,}" in
                dev|debug) PRESET="dev" ;;
                release) PRESET="release" ;;
                win|windows) PRESET="win" ;;
                *) echo "Invalid preset: $OPTARG"; usage ;;
            esac
            ;;
        c) CLEAN=true ;;
        P) PROJECTNAME="$OPTARG" ;;
        t) TIDYUP=true ;;
        h) usage ;;
        *) usage ;;
    esac
done

# 1. Clean directories before building
if [ "$CLEAN" = true ]; then
    echo "--- Cleaning old build and install directories ---"
    rm -rf .build "$PROJECTNAME" "$PROJECTNAME-win"
else
    echo "--- Cleaning old install directory ---"
    rm -rf "$PROJECTNAME" "$PROJECTNAME-win"
fi

# 2. Configure, Build, and Install
echo "--- Configuring, Building, and Installing ($PRESET) ---"
cmake --workflow --preset "$PRESET"

echo "--- Done! Executable is in $PROJECTNAME/ (or $PROJECTNAME-win/) ---"

# 3. Optional post-build tidy up
if [ "$TIDYUP" = true ]; then
    echo "--- Cleaning build folder ---"
    rm -rf .build
fi
