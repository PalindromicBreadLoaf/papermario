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
# src/pc/include comes first so its headers shadow the N64 originals in include/.

include_directories(
    ${CMAKE_SOURCE_DIR}/src/pc/include
    ${CMAKE_SOURCE_DIR}/include
)

# Global compile definitions

add_compile_definitions(
    BUILD_PC
    VERSION_US=1
    _LANGUAGE_C      # gates N64 libultra type definitions in include/PR/*.h
)

# PC executable

add_executable(papermario)

# src/pc/main_pc.c will be the real entry point.
# This is just to keep CMake happy.
target_sources(papermario PRIVATE
    src/pc/main_pc.c

    src/pc/os/os_cache.c
    src/pc/os/os_convert.c
    src/pc/os/os_time.c
    src/pc/os/os_system.c
    src/pc/os/os_thread.c
    src/pc/os/os_mesg.c
    src/pc/os/os_vi.c
    src/pc/os/os_cont.c

    src/pc/nusys/nu_cont.c
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
