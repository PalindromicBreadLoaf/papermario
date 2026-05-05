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

# Dependencies
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

# Prefer a system install for GLM, but fall back to FetchContent so the
# build works without manual setup on any platform.
find_package(glm QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    # Silence deprecation warnings from GLM's own CMakeLists.txt.
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm)
    set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)
endif()

target_link_libraries(papermario PRIVATE
    OpenGL::GL
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer
    glm::glm
)

# SDL2::SDL2main provides the WinMain→main shim on Windows; Not needed on good OSes.
if(TARGET SDL2::SDL2main)
    target_link_libraries(papermario PRIVATE SDL2::SDL2main)
endif()

# Subdirectories
# add_subdirectory(src/pc)
