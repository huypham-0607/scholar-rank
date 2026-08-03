#[[
    To generate Makefiles, run:
    
    cmake -S . -B build

    -S:                 Source directory
    -B:                 Build directory
    _____________________________________________________________________
    
    To compile into binaries, run:
    
    cmake --build build -j$(nproc)

    --build:    Tells CMake to directly invoke build tools in /build
    -j:         Build in parallel using jobs
 
]]

# Declaring oldest version of CMake is allowed to run this file.
# CMake generally insist first command to set minimum version.
cmake_minimum_required(VERSION 3.15)

# Declaring project name and languanges.
project(scholar_rank LANGUAGES CXX)

# Setting C++ standards passed to the compiler & forcing an error if
# detected compiler cannot support set standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Release)

# Defining a variable SCHOLAR_RANK_SOURCES, which will contains our sources.
# Purely organization, but nice to have.
set(SCHOLAR_RANK_SOURCES
    src/utils/logger.cpp
    src/retrieval/index_builder.cpp
)

# Building a library scholar_rank_core compiled from SCHOLAR_RANK_SOURCES.
# Future entry-point files will be linked with this.
add_library(scholar_rank_core ${SCHOLAR_RANK_SOURCES})

target_include_directories(scholar_rank_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)