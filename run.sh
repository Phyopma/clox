#! /bin/bash
set -e

mkdir -p build

cmake -B build -S .

cmake --build build

if [ -n "$1" ]; then
  ./build/clox "$1"
else
  ./build/clox
fi