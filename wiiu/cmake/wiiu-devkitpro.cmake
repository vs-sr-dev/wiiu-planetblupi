# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Samuele Voltan
#
# Planet Blupi — Wii U / devkitPPC + WUT toolchain wrapper.
#
# Layered on top of the official devkitPro CMake support (WiiU.cmake) so we
# inherit powerpc-eabi-gcc, the .rpx output rules and the WUT / portlibs
# search paths for free, then add the Blupi-specific switches.
#
# Unlike the LBA2 port (a raw DOS engine with hand-written OSScreen/AX/VPAD
# backends), Planet Blupi is a pure SDL2 application, so the whole graphics /
# audio / input stack comes from the devkitPro wiiu-sdl2 portlibs and this
# file stays deliberately thin.

# Pull in the upstream devkitPro Wii U toolchain. Shipped inside the
# devkitpro/devkitppc Docker image at this path (pacman package wiiu-cmake
# keeps it here on native installs too).
include($ENV{DEVKITPRO}/cmake/WiiU.cmake)

set(PLATFORM_WIIU TRUE)

add_definitions(
    -D__WIIU__
    -D__WUT__
)

# -ffp-contract=off: GCC on PPC contracts a*b+c into a single fused multiply-
# add, skipping the intermediate rounding x86 performs. Planet Blupi's
# isometric projection and the fixed/float helpers in fix.cxx feed integer
# cell coordinates; keeping every FP op individually rounded avoids ±1 drift
# versus the reference desktop build. Cheap insurance, matches the LBA2 port.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -ffp-contract=off")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-contract=off")
