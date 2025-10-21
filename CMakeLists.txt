cmake_minimum_required(VERSION 3.10)
project(mintvcs)

set(CMAKE_CXX_STANDARD 17)

# Add all source files automatically
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create executable
add_executable(mintvcs ${SOURCES})

# Link zlib (required for hash_object.cpp)
find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    target_include_directories(mintvcs PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(mintvcs PRIVATE ${ZLIB_LIBRARIES})
endif()