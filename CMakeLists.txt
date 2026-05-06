cmake_minimum_required(VERSION 3.16)
project(papermario_pc VERSION 0.1.0 LANGUAGES C)

# -DBUILD_PC=ON selects the host PC build. The existing Ninja/N64 build is
# driven by ./configure + ninja and is completely unaffected by this file.
option(BUILD_PC "Build the PC port (parallel to the N64 Ninja build)" OFF)

if(NOT BUILD_PC)
    message(STATUS "BUILD_PC=OFF - nothing configured. Pass -DBUILD_PC=ON to enable the PC port.")
    return()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-builtin-declaration-mismatch)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src/pc/include
    ${CMAKE_SOURCE_DIR}/src/pc/gfx
    ${CMAKE_SOURCE_DIR}/src/pc/audio
    ${CMAKE_SOURCE_DIR}/include
)

add_compile_definitions(
    BUILD_PC
    VERSION_US=1
    _LANGUAGE_C      # gates N64 libultra type definitions in include/PR/*.h
    F3DEX_GBI_2
)

add_executable(papermario)

target_sources(papermario PRIVATE
    src/pc/main_pc.c
    src/pc/asset_loader.c

    src/pc/os/os_cache.c
    src/pc/os/os_convert.c
    src/pc/os/os_time.c
    src/pc/os/os_system.c
    src/pc/os/os_thread.c
    src/pc/os/os_mesg.c
    src/pc/os/os_vi.c
    src/pc/os/os_cont.c
    src/pc/os/os_pi.c
    src/pc/os/os_sp.c
    src/pc/os/os_ai.c

    src/pc/gfx/glad.c
    src/pc/gfx/tex_conv.c
    src/pc/gfx/rdp_state.c
    src/pc/gfx/gbi_interpreter.c
    src/pc/gfx/gl_backend.c
    src/pc/gfx/texture_cache.c

    src/pc/nusys/nu_cont.c
    src/pc/nusys/nu_sched.c
    src/pc/nusys/nu_gfx.c
    src/pc/nusys/nu_pi.c

    src/pc/audio/audio_pc.c
    src/pc/audio/vadpcm_dec.c
    src/pc/audio/audio_mix.c

    src/pc/nusys/nu_audio.c
)

# Dependencies
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

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
    m
)

# SDL2::SDL2main provides the WinMain→main shim on Windows; Not needed on good OSes.
if(TARGET SDL2::SDL2main)
    target_link_libraries(papermario PRIVATE SDL2::SDL2main)
endif()

# Subdirectories
# add_subdirectory(src/pc)
