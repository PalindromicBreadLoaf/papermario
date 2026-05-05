cmake_minimum_required(VERSION 3.16)
project(papermario_pc VERSION 0.1.0 LANGUAGES C)

# -DBUILD_PC=ON selects the host PC build.  The existing Ninja/N64 build is
# driven by ./configure + ninja and is completely unaffected by this file.
option(BUILD_PC "Build the PC port (parallel to the N64 Ninja build)" OFF)

if(NOT BUILD_PC)
    message(STATUS "BUILD_PC=OFF - nothing configured. Pass -DBUILD_PC=ON to enable the PC port.")
    return()
endif()

# Compiler settings

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Include paths
# These let existing game headers (#include "common.h", <PR/os.h>, etc.) resolve
# without modification. PC-side headers will be added shadow the N64 headers
# where needed.

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/libc
    ${CMAKE_SOURCE_DIR}/include/gcc
)

# Global compile definitions

add_compile_definitions(
    BUILD_PC
    VERSION_US=1
)

# PC executable

add_executable(papermario)

# src/pc/main_pc.c will be the real entry point.
# This is just to keep CMake happy.
target_sources(papermario PRIVATE
    src/pc/main_pc.c
)

target_include_directories(papermario PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pc/include
)

# Subdirectories
# add_subdirectory(src/pc)
