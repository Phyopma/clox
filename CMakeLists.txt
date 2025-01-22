cmake_minimum_required(VERSION 3.5.0)
project(clox VERSION 0.1.0 LANGUAGES C)

# Include directories
include_directories(src/include)

# Find all .c files in the src directory
file(GLOB SOURCES main.c "src/*.c")

# Add executable
add_executable(clox ${SOURCES})


