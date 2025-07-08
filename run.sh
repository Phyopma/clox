#! /bin/bash
set -e

mkdir -p build

cmake -B build -S .
cmake --build build

EXECUTABLE=${2:-clox}

if [ -n "$1" ]; then
  ./build/$EXECUTABLE "$1"
else
  ./build/$EXECUTABLE
fi