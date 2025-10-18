cmake_minimum_required(VERSION 3.10)
project(mintvcs)

set(CMAKE_CXX_STANDARD 17)

# Add all source files automatically
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(mintvcs ${SOURCES})