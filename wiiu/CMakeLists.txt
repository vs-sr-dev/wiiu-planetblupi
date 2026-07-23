# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Samuele Voltan
#
# Planet Blupi — Wii U port build.
#
# Self-contained out-of-tree build: it globs the pristine upstream sources
# from ../planetblupi-master/src and compiles them for devkitPPC/WUT, adding
# a handful of Wii U shims (movie stub, libintl stub, input backend). All
# upstream edits are guarded behind `#ifdef __WIIU__`, so the checkout stays
# diffable against blupi-games/planetblupi.
#
# Configure with the toolchain file:
#   cmake -B build-wiiu -DCMAKE_TOOLCHAIN_FILE=cmake/wiiu-devkitpro.cmake
# (or just run ./build-wiiu.sh which wraps it in the devkitpro/devkitppc
# Docker image).

cmake_minimum_required(VERSION 3.20)
project(planetblupi-wiiu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(PB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../planetblupi-master)
set(PB_SRC  ${PB_ROOT}/src)
set(PB_RES  ${PB_ROOT}/resources)
set(WIIU_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Version / config.h  (generated from the upstream template)
# ---------------------------------------------------------------------------
set(PB_VERSION_MAJOR 1)
set(PB_VERSION_MINOR 15)
set(PB_VERSION_PATCH 0)
set(PB_VERSION_EXTRA "wiiu")
set(CMAKE_INSTALL_BINDIR "bin")
configure_file(${PB_SRC}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/config.h @ONLY)

# ---------------------------------------------------------------------------
# Dependencies (devkitPro wiiu-sdl2 portlibs — preinstalled in the image)
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_mixer REQUIRED)

# Work around a bug in devkitPro's sdl2_mixer-config.cmake: the regex that
# harvests static deps ("-[lm]([-a-zA-Z0-9._]+)") has no '+' in its character
# class, so it clips -lstdc++ (pulled in by modplug) down to a bogus "stdc"
# entry. libstdc.a does not exist (libstdc++.a does), so the link dies with
# "cannot find -lstdc". Repair the imported target's interface list in place.
get_target_property(_mixdeps SDL2_mixer::SDL2_mixer-static INTERFACE_LINK_LIBRARIES)
if(_mixdeps)
  string(REGEX REPLACE "(^|;)stdc(;|$)" "\\1stdc++\\2" _mixdeps "${_mixdeps}")
  set_target_properties(SDL2_mixer::SDL2_mixer-static PROPERTIES
    INTERFACE_LINK_LIBRARIES "${_mixdeps}")
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB PB_SOURCES ${PB_SRC}/*.cxx)
# movie.cxx pulls in SDL_kitchensink (ffmpeg) which has no Wii U port; movie_wiiu
# below replaces it with a Cinepak(.avi) + Ogg(SDL_mixer) FMV backend, decoding
# the movies transcoded offline into wiiu/assets/movie (embedded in the romfs).
list(REMOVE_ITEM PB_SOURCES ${PB_SRC}/movie.cxx)

add_executable(planetblupi
  ${PB_SOURCES}
  ${PB_SRC}/platform/platform_sdl.cxx   # generic SDL event loop, reused as-is
  ${WIIU_SRC}/movie_wiiu.cxx            # FMV: AVI demux + Cinepak + Ogg audio
  ${WIIU_SRC}/cinepak.c                 # self-contained Cinepak decoder
  ${WIIU_SRC}/platform_wiiu.cxx
)

target_include_directories(planetblupi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/compat            # libintl stub — must win first
  ${PB_SRC}
  ${CMAKE_CURRENT_BINARY_DIR}/generated         # config.h
)

# __WIIU__ is already defined by the toolchain; kept here for the odd tool
# that configures without it. USE_CURL/USE_NLS deliberately left undefined.
target_compile_definitions(planetblupi PRIVATE __WIIU__)

# Quiet the legacy-C++ noise from a 1997-era codebase so real errors show.
target_compile_options(planetblupi PRIVATE
  -Wno-narrowing -Wno-write-strings -Wno-format-security)

# The Wii U has no dynamic linking; the SDL2 configs expose both an "auto"
# target and an explicit -static one. Mixing the auto SDL2::SDL2 with the
# satellite libs trips an INTERFACE_SDL2_SHARED mismatch, so link the -static
# variants throughout (SDL2main has no -static variant).
target_link_libraries(planetblupi PRIVATE
  SDL2_ttf::SDL2_ttf-static
  SDL2_image::SDL2_image-static
  SDL2_mixer::SDL2_mixer-static
  SDL2::SDL2main
  SDL2::SDL2-static
  # (VPAD, read directly in platform_wiiu.cxx for touch->mouse, lives in wut's
  # umbrella libwut.a which the wut toolchain already links — no extra lib.)
  # freetype in the ttf stack is built with woff2 support but its config omits
  # brotli from the static dep list. A bare "brotlidec" gets reordered ahead of
  # freetype and dropped by ld (no refs yet). Pass it as a verbatim linker
  # group with freetype so the cyclic static ref resolves regardless of order.
  "-Wl,--start-group,-lfreetype,-lbrotlidec,-lbrotlicommon,--end-group")

# ---------------------------------------------------------------------------
# Package: .rpx + self-contained .wuhb
#
# Unlike LBA2 (retail data you must own), Planet Blupi ships freeware assets,
# so the whole runtime tree is embedded in the .wuhb romfs (mounted at
# /vol/content). Layout mirrors the desktop install: share/planetblupi/...
# MIDI is excluded (OGG is used). Movies are transcoded offline to Cinepak .avi
# + Ogg .ogg (wiiu/assets/movie) and embedded under share/planetblupi/movie.
# ---------------------------------------------------------------------------
set(ROMFS ${CMAKE_CURRENT_BINARY_DIR}/romfs)
set(PBSHARE ${ROMFS}/share/planetblupi)
file(MAKE_DIRECTORY ${PBSHARE})
file(COPY ${PB_RES}/data   DESTINATION ${PBSHARE})
file(COPY ${PB_RES}/image  DESTINATION ${PBSHARE})
file(COPY ${PB_RES}/fonts  DESTINATION ${PBSHARE})
file(COPY ${PB_RES}/sound  DESTINATION ${PBSHARE})
file(COPY ${PB_RES}/music  DESTINATION ${PBSHARE}
     FILES_MATCHING PATTERN "*.ogg")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/movie DESTINATION ${PBSHARE})

wut_create_rpx(planetblupi)
wut_create_wuhb(planetblupi
  OUTPUT    planetblupi.wuhb
  CONTENT   ${ROMFS}
  NAME      "Planet Blupi"
  SHORTNAME "Planet Blupi"
  AUTHOR    "Epsitec/D.Roux/M.Schroeter — Wii U port: S.Voltan"
  ICON      ${PB_ROOT}/blupi.png)
