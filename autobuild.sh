#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Default values
PRESET="release"
CLEAN=false
PROJECT="SystemDesign"
# Help function
usage() {
    echo "Usage: $0 [-p preset] [-c]"
    echo "  -p  Workflow preset: dev, release, win [default: release]"
    echo "  -c  Clean build and install directories before building"
    exit 1
}

# Parse lowercase command-line arguments
while getopts "p:ch" opt; do
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
        h) usage ;;
        *) usage ;;
    esac
done

# 1. Optional Clean
if [ "$CLEAN" = true ]; then
    echo "--- Cleaning old build and install directories ---"
    rm -rf build $PROJECT $PROJECT-win
else
    echo "--- Cleaning old install directory ---"
    rm -rf $PROJECT $PROJECT-win
fi

echo "--- Configuring, Building, and Installing ($PRESET) ---"
cmake --workflow --preset "$PRESET" 

echo "--- Done! Executable is in "$PROJECT"/ (or "$PROJECT"-win/) ---"
