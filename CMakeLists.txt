#============================================================================
# JK2VITA — Jedi Knight II: Jedi Outcast (Single Player) for PlayStation Vita
#
# Single statically-linked ELF -> .self -> .vpk via the VitaSDK toolchain.
# Engine/renderer = OpenJK code/ ; game/cgame/icarus = OpenJK codeJK2/ ; JK2_MODE.
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

set(GAME_TARGET   "JK2VITA")
set(VITA_APP_NAME "JK2VITA: Jedi Outcast")
set(VITA_TITLEID  "JK2VITA01")
set(PORT_VERSION  "0.9.9")
set(VITA_VERSION  "00.99")	# SFO APP_VER is NN.NN and cannot carry the patch digit

#----------------------------------------------------------------------------
# Include domains.
# codeJK2 include dirs FIRST so the game's headers win over the shared code/ versions.
include_directories(
  ${CMAKE_SOURCE_DIR}/src/compat   # newlib gap-fillers (e.g. <endian.h>); must be first
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/codeJK2/game
  ${CMAKE_SOURCE_DIR}/src/codeJK2/cgame
  ${CMAKE_SOURCE_DIR}/src/codeJK2/icarus
  ${CMAKE_SOURCE_DIR}/src/codeJK2
)
include_directories(
  ${CMAKE_SOURCE_DIR}/src/code
  ${CMAKE_SOURCE_DIR}/src/code/game
  ${CMAKE_SOURCE_DIR}/src/code/rd-common
  ${CMAKE_SOURCE_DIR}/src/code/rd-vanilla
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/src/lib/gsl-lite/include
  ${CMAKE_SOURCE_DIR}/src/lib/minizip/include
  ${CMAKE_SOURCE_DIR}/src/lib/minizip/include/minizip
  ${CMAKE_BINARY_DIR}/generated
)

# Generate qcommon/q_version.h (normally produced by OpenJK's build).
set(GIT_TAG "${GAME_TARGET}-${PORT_VERSION}")
set(SOURCE_DATE "${CMAKE_BUILD_TYPE}")
configure_file(
  ${CMAKE_SOURCE_DIR}/src/shared/qcommon/q_version.h.in
  ${CMAKE_BINARY_DIR}/generated/qcommon/q_version.h
  @ONLY)
configure_file(
  ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml.in
  ${CMAKE_BINARY_DIR}/generated/livearea/template.xml
  @ONLY)

#----------------------------------------------------------------------------
# Defines.
#  SP_GAME / _JK2EXE : OpenJK single-player build (engine + game unified)
#  JK2_MODE          : Jedi Outcast behavior
#  VITA / __linux__  : Vita platform + take the unix code paths
#----------------------------------------------------------------------------
add_compile_definitions(
  SP_GAME
  _JK2EXE              # single-player exe marker (both games)
  VITA
  __linux__
  ARCH_STRING="arm"
  NDEBUG
  FINAL_BUILD
  __FLOAT_WORD_ORDER=1
  IOAPI_NO_64          # minizip: use fopen (newlib has no fopen64)
)
add_compile_definitions(JK2_MODE)

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
# Source set (generated).
#----------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/jk2_sources.cmake)

# built here, not taken from the SDK: the installed libSDL2.a wants a GL video
# driver, and other projects share that SDK
set(VIDEO_VITA_VGL OFF CACHE BOOL "" FORCE)
set(VIDEO_VITA_PVR OFF CACHE BOOL "" FORCE)
set(VIDEO_VITA_PIB OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/SDL2 EXCLUDE_FROM_ALL)


set(GXM_SOURCES
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_device.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_texture.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_state.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_backend.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-vanilla/tr_gxm_bridge.cpp
)

set(GXM_SOURCES_NOBRIDGE
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_device.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_texture.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_state.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_backend.cpp
)

add_executable(${GAME_TARGET}.elf ${JK2_SOURCES} ${GXM_SOURCES})
target_compile_definitions(${GAME_TARGET}.elf PRIVATE USE_GXM_NATIVE)

# mp3code's global `window` (float decode table) collides with rd-vanilla's
# `window_t window`. Rename it consistently across the (self-contained) mp3
# decoder only, via a per-file macro -- no source edits, no effect elsewhere.
file(GLOB JK2_MP3_SOURCES ${CMAKE_SOURCE_DIR}/src/code/mp3code/*.c)
set_source_files_properties(${JK2_MP3_SOURCES} PROPERTIES COMPILE_DEFINITIONS "window=mp3code_window")

target_link_libraries(${GAME_TARGET}.elf
  SDL2-static
  # image / compression
  jpeg
  png16
  z
  # math
  mathneon
  stdc++
  # Sce stubs
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
# Separate ELF/SELF (its own GXM context) bundled into the same VPK as
# companion.bin; the engine eboot sceAppMgrLoadExec's it when launched with -config.
#----------------------------------------------------------------------------
option(JK2_BUILD_COMPANION "Build the configuration app and bundle it into the VPK" OFF)
if(JK2_BUILD_COMPANION)

add_executable(companion.elf ${CMAKE_SOURCE_DIR}/src/vita/companion_main.cpp ${GXM_SOURCES_NOBRIDGE})
target_link_libraries(companion.elf
  mathneon
  stdc++
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

endif()

#----------------------------------------------------------------------------
# gxm_probe: standalone hardware check for the native GXM renderer. Separate VPK
# because sceGxmInitialize is process-global and only one context may hold it.
#----------------------------------------------------------------------------
option(JK2_BUILD_GXM_PROBE "Also build the standalone GXM hardware probe VPK" OFF)
if(JK2_BUILD_GXM_PROBE)

add_executable(gxm_probe.elf
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_device.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_texture.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_testscene.cpp
  ${CMAKE_SOURCE_DIR}/src/code/rd-gxm/gxm_probe.cpp
)
target_compile_definitions(gxm_probe.elf PRIVATE VITA)
target_link_libraries(gxm_probe.elf
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceSysmodule_stub
  SceAppMgr_stub
  c
  m
)
vita_create_self(gxm_probe.self gxm_probe.elf UNSAFE)
vita_create_vpk(gxm_probe.vpk GXMPROBE1 gxm_probe.self
  VERSION 01.00
  NAME "GXM Probe"
)

endif()

#----------------------------------------------------------------------------
# Package: ELF -> SELF -> VPK
#----------------------------------------------------------------------------
set(JK2_VPK_EXTRA_FILES)
if(JK2_BUILD_COMPANION)
  list(APPEND JK2_VPK_EXTRA_FILES FILE ${CMAKE_BINARY_DIR}/companion.bin companion.bin)
endif()

vita_create_self(${GAME_TARGET}.self ${GAME_TARGET}.elf UNSAFE)

set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(${GAME_TARGET}.vpk ${VITA_TITLEID} ${GAME_TARGET}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_BINARY_DIR}/generated/livearea/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/config.png sce_sys/livearea/contents/config.png
  ${JK2_VPK_EXTRA_FILES}
)
