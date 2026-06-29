#============================================================================
# JK2VITA — Jedi Knight II: Jedi Outcast (Single Player) for PlayStation Vita
#
# Single statically-linked ELF -> .self -> .vpk via the VitaSDK toolchain.
# Engine/renderer = OpenJK code/ ; game/cgame/icarus = OpenJK codeJK2/ ; JK2_MODE.
# See docs/ARCHITECTURE.md and docs/ROADMAP.md.
#============================================================================
cmake_minimum_required(VERSION 3.10)

# --- VitaSDK toolchain ---
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define the VITASDK environment variable (point at your VitaSDK root).")
  endif()
endif()

# Allow older bundled CMake snippets to configure under CMake 4.x.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(JK2VITA C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "JK2VITA: Jedi Outcast")
set(VITA_TITLEID  "JK2VITA01")
set(VITA_VERSION  "00.01")

#----------------------------------------------------------------------------
# Include domains.
# codeJK2/{game,cgame,icarus} come FIRST so the JK2 game-interface headers win
# over the shared code/ versions; code/game is kept after as a fallback for
# engine-only headers not present under codeJK2/.
#----------------------------------------------------------------------------
include_directories(
  ${CMAKE_SOURCE_DIR}/src/compat   # newlib gap-fillers (e.g. <endian.h>); must be first
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/codeJK2/game
  ${CMAKE_SOURCE_DIR}/src/codeJK2/cgame
  ${CMAKE_SOURCE_DIR}/src/codeJK2/icarus
  ${CMAKE_SOURCE_DIR}/src/codeJK2
  ${CMAKE_SOURCE_DIR}/src/code
  ${CMAKE_SOURCE_DIR}/src/code/game
  ${CMAKE_SOURCE_DIR}/src/code/rd-common
  ${CMAKE_SOURCE_DIR}/src/code/rd-vanilla
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/src/lib/gsl-lite/include
  ${CMAKE_SOURCE_DIR}/src/lib/minizip/include
  ${CMAKE_SOURCE_DIR}/src/lib/minizip/include/minizip
  ${CMAKE_BINARY_DIR}/generated
  $ENV{VITASDK}/arm-vita-eabi/include/SDL2
)

# Generate qcommon/q_version.h (normally produced by OpenJK's build).
set(GIT_TAG "JK2VITA-0.1")
set(SOURCE_DATE "${CMAKE_BUILD_TYPE}")
configure_file(
  ${CMAKE_SOURCE_DIR}/src/shared/qcommon/q_version.h.in
  ${CMAKE_BINARY_DIR}/generated/qcommon/q_version.h
  @ONLY)

#----------------------------------------------------------------------------
# Defines.
#  SP_GAME / _JK2EXE : OpenJK single-player build (engine + game unified)
#  JK2_MODE          : Jedi Outcast (vs Jedi Academy) behavior
#  VITA / __linux__  : Vita platform + take the unix code paths
#----------------------------------------------------------------------------
add_compile_definitions(
  SP_GAME
  _JK2EXE
  JK2_MODE
  VITA
  __linux__
  ARCH_STRING="arm"
  NDEBUG
  FINAL_BUILD
  __FLOAT_WORD_ORDER=1
  IOAPI_NO_64          # minizip: use fopen (newlib has no fopen64)
)

#----------------------------------------------------------------------------
# Flags (StaticJK-proven ARM/NEON hard-float set).
#----------------------------------------------------------------------------
set(JK2VITA_ARCH_FLAGS
  -g -O3
  -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -ffast-math -fsigned-char -fno-short-enums
  -fsingle-precision-constant -fno-optimize-sibling-calls
)
add_compile_options(${JK2VITA_ARCH_FLAGS})
# C++ needs permissive (legacy id-Tech-3 era code) + GNU visibility.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-D__GNU_VISIBLE>)

#----------------------------------------------------------------------------
# Source set (generated: see cmake/jk2_sources.cmake + docs/PORTING_NOTES N-0004).
#----------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/jk2_sources.cmake)

add_executable(${PROJECT_NAME}.elf ${JK2_SOURCES})

# mp3code's global `window` (float decode table) collides with rd-vanilla's
# `window_t window`. Rename it consistently across the (self-contained) mp3
# decoder only, via a per-file macro -- no source edits, no effect elsewhere.
file(GLOB JK2_MP3_SOURCES ${CMAKE_SOURCE_DIR}/src/code/mp3code/*.c)
set_source_files_properties(${JK2_MP3_SOURCES} PROPERTIES COMPILE_DEFINITIONS "window=mp3code_window")

target_link_libraries(${PROJECT_NAME}.elf
  # Vita GL stack
  SDL2
  vitaGL
  vitashark
  SceShaccCgExt
  # image / compression
  jpeg
  png16
  z
  # math
  mathneon
  # C++ runtime (vitaGL + OpenJK are C++)
  stdc++
  # Sce stubs
  taihen_stub
  SceShaccCg_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceMotion_stub
  SceHid_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCommonDialog_stub
  SceIme_stub
  SceSysmodule_stub
  SceKernelDmacMgr_stub
  SceAppMgr_stub
  SceAppUtil_stub
  ScePower_stub
  SceNet_stub
  SceNetCtl_stub
  c
  m
)

#----------------------------------------------------------------------------
# Companion: standalone "Configuration" app launched by the LiveArea -config gate.
# Separate ELF/SELF (its own vitaGL context) bundled into the same VPK as
# companion.bin; the engine eboot sceAppMgrLoadExec's it when launched with -config.
#----------------------------------------------------------------------------
add_executable(companion.elf ${CMAKE_SOURCE_DIR}/src/vita/companion_main.cpp)
target_link_libraries(companion.elf
  vitaGL
  vitashark
  SceShaccCgExt
  mathneon
  stdc++
  taihen_stub
  SceShaccCg_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceSysmodule_stub
  SceKernelDmacMgr_stub
  SceAppMgr_stub
  SceAppUtil_stub
  ScePower_stub
  c
  m
)
vita_create_self(companion.bin companion.elf UNSAFE)

#----------------------------------------------------------------------------
# Package: ELF -> SELF -> VPK
#----------------------------------------------------------------------------
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME}.elf UNSAFE)

set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/config.png sce_sys/livearea/contents/config.png
  FILE ${CMAKE_BINARY_DIR}/companion.bin companion.bin
)
