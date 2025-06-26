#! /bin/bash
set -e

mkdir -p build

cmake -B build -S .

cmake --build build

./build/clox "$1"