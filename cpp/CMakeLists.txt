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

    _____________________________________________________________________

    For running tests, several options:
    
        ctest --test-dir build

    CMake bundled test runner, good for running whole suite together.

        ./build/*_tests

    Run the compiled binary directly, suitable to isolated runs.
]]

# Declaring oldest version of CMake is allowed to run this file.
# CMake generally insist first command to set minimum version.
cmake_minimum_required(VERSION 3.15)

# Declaring project name and languanges.
project(scholar_rank LANGUAGES CXX)

# Includes
include(FetchContent)

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
    src/utils/vbe.cpp
    src/utils/file_io.cpp
    src/retrieval/posting_list.cpp
    src/retrieval/bm25.cpp
    src/retrieval/token_stream.cpp
    src/retrieval/construct_inverted_blocks.cpp
    src/retrieval/construct_doc_len_list.cpp
    src/retrieval/merge_inverted_blocks.cpp
)

# Building a library scholar_rank_core compiled from SCHOLAR_RANK_SOURCES.
# Future entry-point files will be linked with this.
add_library(scholar_rank_core ${SCHOLAR_RANK_SOURCES})

target_include_directories(scholar_rank_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Adding CLI entry points (apps/)
add_executable(build_inverted_blocks apps/build_inverted_blocks.cpp)
target_link_libraries(build_inverted_blocks PRIVATE scholar_rank_core)
add_executable(build_doc_len_list apps/build_doc_len_list.cpp)
target_link_libraries(build_doc_len_list PRIVATE scholar_rank_core)
add_executable(merge_inverted_blocks apps/merge_inverted_blocks.cpp)
target_link_libraries(merge_inverted_blocks PRIVATE scholar_rank_core)
# Adding tests
enable_testing()
FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.15.2)
FetchContent_MakeAvailable(googletest)

add_executable(vbe_tests tests/utils/vbe_tests.cpp)
target_link_libraries(vbe_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(file_io_tests tests/utils/file_io_tests.cpp)
target_link_libraries(file_io_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(posting_list_tests tests/retrieval/posting_list_tests.cpp)
target_link_libraries(posting_list_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(bm25_tests tests/retrieval/bm25_tests.cpp)
target_link_libraries(bm25_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(token_stream_tests tests/retrieval/token_stream_tests.cpp)
target_link_libraries(token_stream_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(construct_inverted_blocks_tests tests/retrieval/construct_inverted_blocks_tests.cpp)
target_link_libraries(construct_inverted_blocks_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(construct_doc_len_list_tests tests/retrieval/construct_doc_len_list_tests.cpp)
target_link_libraries(construct_doc_len_list_tests PRIVATE scholar_rank_core GTest::gtest_main)

add_executable(merge_inverted_blocks_tests tests/retrieval/merge_inverted_blocks_tests.cpp)
target_link_libraries(merge_inverted_blocks_tests PRIVATE scholar_rank_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vbe_tests)
gtest_discover_tests(file_io_tests)
gtest_discover_tests(posting_list_tests)
gtest_discover_tests(bm25_tests)
gtest_discover_tests(token_stream_tests)
gtest_discover_tests(construct_inverted_blocks_tests)
gtest_discover_tests(construct_doc_len_list_tests)
gtest_discover_tests(merge_inverted_blocks_tests)