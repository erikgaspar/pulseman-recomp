# Parte nativa do Pulseman para Android (libmain.so + libSDL2.so), montada por android/build.ps1
# com o toolchain do NDK. É a versão distribuível (PM_DIST): nada gerado a partir da ROM; o jogo
# roda no Musashi direto da ROM do jogador.
#
# PM_HOST_GEN: pasta gen de um build da versão distribuível no PC (build_dist/gen), de onde vêm
# os arquivos do Musashi gerados pelo m68kmake (ele precisa rodar no PC, não no celular).
cmake_minimum_required(VERSION 3.20)
project(pulseman_android C CXX)

get_filename_component(PM_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(PM_HOST_GEN "${PM_ROOT}/build_dist/gen" CACHE PATH "gen de um build_dist no PC")
set(MUSASHI_DIR "${PM_ROOT}/third_party/Musashi-master")
if(NOT EXISTS "${PM_HOST_GEN}/musashi/m68kops.c")
    message(FATAL_ERROR "falta ${PM_HOST_GEN}/musashi/m68kops.c: compile antes a versão distribuível no PC (build_dist)")
endif()

# tabelas da versão distribuível (ajustes e ganchos de recomp/patches.txt), geradas no PC
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(DIST_DIR "${CMAKE_BINARY_DIR}/gen_dist")
execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${PM_ROOT}/recomp/gen_dist.py" "${DIST_DIR}"
                RESULT_VARIABLE DIST_RESULT)
if(NOT DIST_RESULT EQUAL 0)
    message(FATAL_ERROR "recomp/gen_dist.py falhou")
endif()

# SDL2 compilado do código-fonte (tools/SDL2-2.30.8-src)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${PM_ROOT}/tools/SDL2-2.30.8-src" SDL2)

add_library(musashi_dist STATIC
    "${MUSASHI_DIR}/m68kcpu.c" "${MUSASHI_DIR}/m68kdasm.c" "${MUSASHI_DIR}/softfloat/softfloat.c"
    "${PM_HOST_GEN}/musashi/m68kops.c")
target_include_directories(musashi_dist PUBLIC "${MUSASHI_DIR}" "${PM_HOST_GEN}/musashi")
target_compile_definitions(musashi_dist PRIVATE M68K_INSTRUCTION_HOOK=M68K_OPT_ON)
target_compile_options(musashi_dist PRIVATE -w)

set(Z80_DIR "${PM_ROOT}/third_party/z80-master")
set(YMFM_DIR "${PM_ROOT}/third_party/ymfm-main/src")
add_library(z80core STATIC "${Z80_DIR}/z80.c")
target_include_directories(z80core PUBLIC "${Z80_DIR}")
target_compile_options(z80core PRIVATE -w)
add_library(ymfm STATIC
    "${YMFM_DIR}/ymfm_opn.cpp" "${YMFM_DIR}/ymfm_ssg.cpp" "${YMFM_DIR}/ymfm_adpcm.cpp" "${YMFM_DIR}/ymfm_misc.cpp")
target_include_directories(ymfm PUBLIC "${YMFM_DIR}")
target_compile_features(ymfm PUBLIC cxx_std_14)
target_compile_options(ymfm PRIVATE -w)

include("${PM_ROOT}/cmake/runtime_sources.cmake")
add_library(main SHARED ${RUNTIME_SOURCES} "${PM_ROOT}/runtime/dist.c" "${DIST_DIR}/dist_table.c")
target_include_directories(main PRIVATE "${PM_ROOT}/runtime")
target_compile_definitions(main PRIVATE PM_DIST)
target_link_libraries(main PRIVATE musashi_dist z80core ymfm SDL2 log android m)
