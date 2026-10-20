cmake_minimum_required(VERSION 3.20)
project(pulseman_recomp C CXX)

set(PM_ROM "${CMAKE_SOURCE_DIR}/../Pulseman_Japan.md" CACHE FILEPATH "ROM do Pulseman (Japão)")
set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")
set(MUSASHI_DIR "${CMAKE_SOURCE_DIR}/third_party/Musashi-master")

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

# runtime do C estático: o .exe não depende do Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /bigobj /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ---------- código recompilado (gerado a partir da ROM do usuário) ----------
# Sem a ROM, só a versão distribuível (pulseman_dist) é compilada.
file(GLOB RECOMP_PY "${CMAKE_SOURCE_DIR}/recomp/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${RECOMP_PY} "${CMAKE_SOURCE_DIR}/recomp/extra_entries.txt" "${CMAKE_SOURCE_DIR}/recomp/overrides.txt"
    "${CMAKE_SOURCE_DIR}/recomp/patches.txt" "${CMAKE_SOURCE_DIR}/recomp/rom.sha1")
set(PM_HAVE_ROM OFF)
if(EXISTS "${PM_ROM}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PM_ROM}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/recomp/gen.py" "${PM_ROM}" "${GEN_DIR}"
        RESULT_VARIABLE GEN_RESULT)
    if(NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "recomp/gen.py falhou (ROM: ${PM_ROM})")
    endif()
    file(STRINGS "${GEN_DIR}/sources.txt" GEN_NAMES)
    set(GEN_SOURCES "${GEN_DIR}/block_table.c")
    foreach(n ${GEN_NAMES})
        list(APPEND GEN_SOURCES "${GEN_DIR}/${n}")
    endforeach()
    set(PM_HAVE_ROM ON)
else()
    message(STATUS "ROM não encontrada (${PM_ROM}): só a versão distribuível")
endif()

# ---------- versão distribuível: tabelas sem a ROM (recomp/gen_dist.py) ----------
set(DIST_DIR "${CMAKE_BINARY_DIR}/gen_dist")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/recomp/gen_dist.py" "${DIST_DIR}"
    RESULT_VARIABLE DIST_RESULT)
if(NOT DIST_RESULT EQUAL 0)
    message(FATAL_ERROR "recomp/gen_dist.py falhou")
endif()

# ---------- Musashi (referência e fallback do interpretador) ----------
add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")
add_custom_command(
    OUTPUT "${GEN_DIR}/musashi/m68kops.c" "${GEN_DIR}/musashi/m68kops.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN_DIR}/musashi"
    COMMAND m68kmake "${GEN_DIR}/musashi" "${MUSASHI_DIR}/m68k_in.c"
    DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c")
add_library(musashi STATIC
    "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_DIR}/softfloat/softfloat.c"
    "${GEN_DIR}/musashi/m68kops.c")
target_include_directories(musashi PUBLIC "${MUSASHI_DIR}" "${GEN_DIR}/musashi")
# a versão distribuível usa o gancho antes de cada instrução (ajustes e rotinas em C)
add_library(musashi_dist STATIC
    "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_DIR}/softfloat/softfloat.c"
    "${GEN_DIR}/musashi/m68kops.c")
target_include_directories(musashi_dist PUBLIC "${MUSASHI_DIR}" "${GEN_DIR}/musashi")
target_compile_definitions(musashi_dist PRIVATE M68K_INSTRUCTION_HOOK=M68K_OPT_ON)
if(MSVC)
    target_compile_options(musashi PRIVATE /W0)
    target_compile_options(musashi_dist PRIVATE /W0)
endif()

# ---------- som: Z80 (superzazu/z80, MIT) e YM2612 (ymfm, BSD-3) ----------
set(Z80_DIR "${CMAKE_SOURCE_DIR}/third_party/z80-master")
set(YMFM_DIR "${CMAKE_SOURCE_DIR}/third_party/ymfm-main/src")
add_library(z80core STATIC "${Z80_DIR}/z80.c")
target_include_directories(z80core PUBLIC "${Z80_DIR}")
add_library(ymfm STATIC
    "${YMFM_DIR}/ymfm_opn.cpp" "${YMFM_DIR}/ymfm_ssg.cpp" "${YMFM_DIR}/ymfm_adpcm.cpp" "${YMFM_DIR}/ymfm_misc.cpp")
target_include_directories(ymfm PUBLIC "${YMFM_DIR}")
target_compile_features(ymfm PUBLIC cxx_std_14)
if(MSVC)
    target_compile_options(z80core PRIVATE /W0)
    target_compile_options(ymfm PRIVATE /W0)
endif()

# ---------- SDL2 ----------
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/tools/SDL2-2.30.8/cmake")
find_package(SDL2 REQUIRED CONFIG)

# ---------- executáveis ----------
set(RUNTIME_SOURCES
    runtime/main.c runtime/cpu.c runtime/bus.c runtime/vdp.c runtime/io.c
    runtime/sha1.c runtime/musashi_glue.c runtime/lockstep.c runtime/sound.c runtime/ym_shim.cpp runtime/menu.c
    runtime/filters.c runtime/savestate.c runtime/png.c runtime/intro_logos.c
    runtime/game/map_stream.c runtime/game/hud.c runtime/game/objects.c)

if(WIN32)   # ícone do .exe (e da janela)
    list(APPEND RUNTIME_SOURCES runtime/pulseman.rc)
endif()

function(pm_game target)
    add_executable(${target} ${RUNTIME_SOURCES} ${GEN_SOURCES})
    target_include_directories(${target} PRIVATE runtime "${GEN_DIR}")
    target_link_libraries(${target} PRIVATE musashi z80core ymfm SDL2::SDL2)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${target}>)
endfunction()

if(PM_HAVE_ROM)
    pm_game(pulseman)
    pm_game(pulseman_lockstep)
    target_compile_definitions(pulseman_lockstep PRIVATE PM_LOCKSTEP)
    if(MSVC)   # o jogo abre sem janela de console (o lockstep continua de console)
        set_target_properties(pulseman PROPERTIES WIN32_EXECUTABLE TRUE LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()

# Versão distribuível: nenhum arquivo gerado a partir da ROM; o jogo roda no interpretador
# direto da ROM do jogador (é a que vai no instalador)
add_executable(pulseman_dist ${RUNTIME_SOURCES} runtime/dist.c "${DIST_DIR}/dist_table.c")
target_include_directories(pulseman_dist PRIVATE runtime)
target_compile_definitions(pulseman_dist PRIVATE PM_DIST)
target_link_libraries(pulseman_dist PRIVATE musashi_dist z80core ymfm SDL2::SDL2)
add_custom_command(TARGET pulseman_dist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:pulseman_dist>)
if(MSVC)
    set_target_properties(pulseman_dist PROPERTIES WIN32_EXECUTABLE TRUE LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()
