cmake_minimum_required(VERSION 3.10)
project(mintvcs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static linking
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Add all source files automatically
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create executable
add_executable(mintvcs ${SOURCES})

# Find and link zlib statically
find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    target_include_directories(mintvcs PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(mintvcs PRIVATE ${ZLIB_LIBRARIES})
endif()

# Additional static linking
target_link_libraries(mintvcs PRIVATE -static)
set_target_properties(mintvcs PROPERTIES LINK_FLAGS "-static")