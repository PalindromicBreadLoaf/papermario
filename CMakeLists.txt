cmake_minimum_required(VERSION 3.16)
project(papermario_pc VERSION 0.1.0 LANGUAGES C ASM)

# -DBUILD_PC=ON selects the host PC build. The existing Ninja/N64 build is
# driven by ./configure + ninja and is completely unaffected by this file.
option(BUILD_PC "Build the PC port (parallel to the N64 Ninja build)" ON)

if(NOT BUILD_PC)
    message(STATUS "BUILD_PC=OFF - nothing configured. Pass -DBUILD_PC=ON to enable the PC port.")
    return()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Keep GCC extensions enabled (gnu11).
set(CMAKE_C_EXTENSIONS ON)

if(MSVC)
    add_compile_options(/W3 /wd4996)
else()
    add_compile_options(
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-builtin-declaration-mismatch
        -Wno-missing-braces
        -Wno-implicit-function-declaration
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -Wno-pointer-sign
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wno-discarded-qualifiers
        -Wno-address
        -Wno-overflow
        -Wno-stringop-overflow
        -Wno-format
        -Wno-enum-conversion
        -Wno-return-mismatch
        -fcommon
        # Ensure static/BSS addresses fit in 32 bits so (s32) pointer casts in game
        # scripts (EVT, HUD, entity) remain valid on x86-64.
        -fno-pie
        # Force-include the PC prelude before every TU to disable MIPS inline asm.
        -include ${CMAKE_SOURCE_DIR}/src/pc/include/pc_prelude.h
    )
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src/pc/include
    ${CMAKE_SOURCE_DIR}/src/pc/gfx
    ${CMAKE_SOURCE_DIR}/src/pc/audio
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ver/us/build/include
    ${CMAKE_SOURCE_DIR}/assets/us
    ${CMAKE_SOURCE_DIR}/src
)

add_compile_definitions(
    BUILD_PC
    VERSION_US=1
    VERSION=us
    _LANGUAGE_C
    F3DEX_GBI_2
    MODERN_COMPILER
    AVOID_UB
)

# Collect all compilable game source files
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")

# Exclude N64-only directories
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/os/.*")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/pc/.*")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/boot/.*")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/gcc/.*")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/dead/.*")
list(FILTER GAME_SRC EXCLUDE REGEX ".*\\.inc\\.c$")

# Exclude PAL-only locale files (German, French, Spanish)
# TODO: Expand beyond US-only build
list(FILTER GAME_SRC EXCLUDE REGEX ".*_de\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*_fr\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*_es\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*_en_de\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/filemenu/filemenu_selectlanguage\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/titlemenu\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/area_hos/hos_10/narrator\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/.*_obfuscated\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/load_obfuscation_shims\\.c$")

# Exclude N64 IS-Viewer debug output
list(FILTER GAME_SRC EXCLUDE REGEX ".*/is_debug\\.c$")

# PC audio supplies host implementations of the libaudio synthesis entry points.
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/audio/core/syn_driver\\.c$")

set(INCLUDED_GAME_SRC)
foreach(GAME_FILE IN LISTS GAME_SRC)
    file(READ "${GAME_FILE}" GAME_FILE_TEXT)
    string(REGEX MATCHALL "#[ \t]*include[ \t]+\"[^\"]+\\.c\"" GAME_FILE_INCLUDES "${GAME_FILE_TEXT}")
    get_filename_component(GAME_FILE_DIR "${GAME_FILE}" DIRECTORY)

    foreach(GAME_FILE_INCLUDE IN LISTS GAME_FILE_INCLUDES)
        string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" GAME_FILE_INCLUDE_PATH "${GAME_FILE_INCLUDE}")
        if(GAME_FILE_INCLUDE_PATH MATCHES "\\.inc\\.c$")
            continue()
        endif()

        set(GAME_FILE_INCLUDE_CANDIDATES
            "${GAME_FILE_DIR}/${GAME_FILE_INCLUDE_PATH}"
            "${CMAKE_SOURCE_DIR}/src/${GAME_FILE_INCLUDE_PATH}"
        )

        foreach(GAME_FILE_INCLUDE_CANDIDATE IN LISTS GAME_FILE_INCLUDE_CANDIDATES)
            get_filename_component(GAME_FILE_INCLUDE_ABS "${GAME_FILE_INCLUDE_CANDIDATE}" ABSOLUTE)
            if(EXISTS "${GAME_FILE_INCLUDE_ABS}")
                list(APPEND INCLUDED_GAME_SRC "${GAME_FILE_INCLUDE_ABS}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(INCLUDED_GAME_SRC)
    list(REMOVE_DUPLICATES INCLUDED_GAME_SRC)
    list(REMOVE_ITEM GAME_SRC ${INCLUDED_GAME_SRC})
endif()

# These are included by parent translation units and are not valid standalone sources
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/effects/effect_table\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/area_dro/dro_01/npc_composer\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/area_dro/dro_01/npc_hint_dryite\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/area_dro/dro_01/npc_hint_dryite_companion\\.c$")
list(FILTER GAME_SRC EXCLUDE REGEX ".*/src/world/area_dro/dro_01/npc_shop_owner\\.c$")

set_source_files_properties(${GAME_SRC} PROPERTIES
    COMPILE_DEFINITIONS "PC_GAME_SOURCE"
)

set(PC_WIDE_CONSTANT_INIT_SRC
    ${CMAKE_SOURCE_DIR}/src/38F00.c
    ${CMAKE_SOURCE_DIR}/src/5B320.c
    ${CMAKE_SOURCE_DIR}/src/battle/action_cmd/three_chances.c
    ${CMAKE_SOURCE_DIR}/src/battle/action_cmd/whirlwind.c
    ${CMAKE_SOURCE_DIR}/src/battle/area/kzn2/actor/lava_bud.c
    ${CMAKE_SOURCE_DIR}/src/battle/area/kzn2/actor/lava_piranha.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/strange_cake.c
    ${CMAKE_SOURCE_DIR}/src/battle/entity_model_icons.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/hammer/hammer_throw.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/coconut.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/dusty_hammer.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/egg_missile.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/insecticide_herb.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/mystery.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/pebble.c
    ${CMAKE_SOURCE_DIR}/src/battle/move/item/sleepy_sheep.c
    ${CMAKE_SOURCE_DIR}/src/battle_ui_gfx.c
    ${CMAKE_SOURCE_DIR}/src/entity/Block.c
    ${CMAKE_SOURCE_DIR}/src/entity/BlueWarpPipe.c
    ${CMAKE_SOURCE_DIR}/src/entity/Chest.c
    ${CMAKE_SOURCE_DIR}/src/entity/HeartBlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/HiddenPanel.c
    ${CMAKE_SOURCE_DIR}/src/entity/ItemBlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/SaveBlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/Shadow.c
    ${CMAKE_SOURCE_DIR}/src/entity/ShatteringBlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/ShatteringBlock_common.c
    ${CMAKE_SOURCE_DIR}/src/entity/Signpost.c
    ${CMAKE_SOURCE_DIR}/src/entity/SimpleSpring.c
    ${CMAKE_SOURCE_DIR}/src/entity/SuperBlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/Switch.c
    ${CMAKE_SOURCE_DIR}/src/entity/WoodenCrate.c
    ${CMAKE_SOURCE_DIR}/src/entity/default/BoardedFloor.c
    ${CMAKE_SOURCE_DIR}/src/entity/default/BombableRock.c
    ${CMAKE_SOURCE_DIR}/src/entity/default/Padlock.c
    ${CMAKE_SOURCE_DIR}/src/entity/jan_iwa/ArrowSign.c
    ${CMAKE_SOURCE_DIR}/src/entity/jan_iwa/Plants1.c
    ${CMAKE_SOURCE_DIR}/src/entity/jan_iwa/Plants2.c
    ${CMAKE_SOURCE_DIR}/src/entity/sbk_omo/StarBoxLauncher.c
    ${CMAKE_SOURCE_DIR}/src/entity/sbk_omo/Tweester.c
    ${CMAKE_SOURCE_DIR}/src/entity_model.c
    ${CMAKE_SOURCE_DIR}/src/filemenu/filemenu_msg.c
    ${CMAKE_SOURCE_DIR}/src/global_hud_scripts.c
    ${CMAKE_SOURCE_DIR}/src/hud_element.c
    ${CMAKE_SOURCE_DIR}/src/level_up.c
    ${CMAKE_SOURCE_DIR}/src/starpoint.c
    ${CMAKE_SOURCE_DIR}/src/starpoint_en.c
    ${CMAKE_SOURCE_DIR}/src/menu_hud_scripts.c
    ${CMAKE_SOURCE_DIR}/src/pause/pause_gfx.c
    ${CMAKE_SOURCE_DIR}/src/world/area_kpa/kpa_82/npc.c
    ${CMAKE_SOURCE_DIR}/src/world/area_kmr/kmr_22/npc.c
    ${CMAKE_SOURCE_DIR}/src/world/area_kzn/kzn_19/npc.c
    ${CMAKE_SOURCE_DIR}/src/battle/partner_dma.c
)
set_source_files_properties(${PC_WIDE_CONSTANT_INIT_SRC} PROPERTIES
    COMPILE_DEFINITIONS "s32=intptr_t;u32=uintptr_t;PC_GAME_SOURCE"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/draw_box.c PROPERTIES
    COMPILE_DEFINITIONS "s32=intptr_t;u32=uintptr_t;PC_GAME_SOURCE;PC_BUILDING_DRAW_BOX"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/windows.c PROPERTIES
    COMPILE_DEFINITIONS "PC_GAME_SOURCE;PC_BUILDING_WINDOWS"
)

add_executable(papermario)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_options(papermario PRIVATE
        -Wl,--allow-multiple-definition
    )
endif()

target_sources(papermario PRIVATE
    src/pc/main_pc.c
    src/pc/asset_loader.c
    src/pc/effect_shims_pc.c
    src/pc/missing_stubs_pc.c
    assets/us/imgfx/imgfx_data.c

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
    src/pc/os/os_internal_reg.c
    src/pc/os/os_tlb.c
    src/pc/os/os_gu.c
    src/os/coss.c
    src/os/frustum.c
    src/os/guLookAt.c
    src/os/guMtxXFMF.c
    src/os/guMtxXFML.c
    src/os/guOrtho.c
    src/os/guRotate.c
    src/os/lookatref.c
    src/os/lookathil.c
    src/os/perspective.c
    src/os/position.c
    src/os/rotateRPY.c
    src/os/sins.c
    src/pc/os/decode_yay0.c
    src/pc/os/os_libc.c
    src/pc/os/os_sched.c
    src/pc/os/sk_api.c

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
    src/pc/nusys/nu_boot.c

    src/pc/audio/audio_pc.c
    src/pc/audio/vadpcm_dec.c
    src/pc/audio/audio_mix.c
    src/pc/audio/syn_driver_pc.c

    src/pc/nusys/nu_audio.c

    src/pc/os/dma_wrap_pc.c

    src/pc/pc_ld_symbols.S

    ${GAME_SRC}
)

# Pass absolute asset paths for .incbin in include_asset.h (must come after add_executable)
target_compile_definitions(papermario PRIVATE
    "PC_ASSET_DIR=\"${CMAKE_SOURCE_DIR}/ver/us/build\""
    "PC_RAW_ASSET_DIR=\"${CMAKE_SOURCE_DIR}/ver/us/build/assets/us\""
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    file(READ "${CMAKE_SOURCE_DIR}/assets/us/effects/effect_table.c" PC_EFFECT_TABLE_TEXT)
    string(REGEX MATCHALL "FX_ENTRY\\([A-Za-z0-9_]+," PC_EFFECT_TABLE_ENTRIES "${PC_EFFECT_TABLE_TEXT}")
    foreach(PC_EFFECT_TABLE_ENTRY IN LISTS PC_EFFECT_TABLE_ENTRIES)
        string(REGEX REPLACE "FX_ENTRY\\(([A-Za-z0-9_]+)," "\\1" PC_EFFECT_NAME "${PC_EFFECT_TABLE_ENTRY}")
        target_link_options(papermario PRIVATE
            "-Wl,--defsym=fx_${PC_EFFECT_NAME}=${PC_EFFECT_NAME}_main"
        )
    endforeach()
endif()

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

# Wrap dma_copy/dma_write with PC implementations from dma_wrap_pc.c.
# Redirect (s32) pointer casts in game script arrays to stay in low 32-bit address
# space by building non-PIE so BSS/text addresses always fit in a 32-bit integer.
if(NOT MSVC)
    target_link_options(papermario PRIVATE
        -no-pie
        -Wl,--wrap=dma_copy
        -Wl,--wrap=dma_write
    )
endif()
