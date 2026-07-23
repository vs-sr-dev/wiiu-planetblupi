#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Samuele Voltan
#
# Planet Blupi — Wii U build wrapper.
#
# Runs the devkitPro/devkitPPC + WUT toolchain inside the official
# devkitpro/devkitppc Docker image and produces a self-contained
# planetblupi.wuhb (freeware assets embedded in the romfs — no bring-your-own
# data step, unlike the LBA2 port).
#
# Usage:
#   ./build-wiiu.sh              # configure + build + package
#   ./build-wiiu.sh configure    # configure only
#   ./build-wiiu.sh clean        # rm -rf build-wiiu/
#   ./build-wiiu.sh shell        # interactive shell in the container
#
# Environment overrides:
#   DOCKER    docker binary          (default: docker)
#   IMAGE     image tag              (default: devkitpro/devkitppc:latest)
#   BUILDDIR  out-of-source dir      (default: build-wiiu)
#   JOBS      make -j value          (default: 8)

set -e

DOCKER="${DOCKER:-docker}"
IMAGE="${IMAGE:-devkitpro/devkitppc:latest}"
SRCDIR="$(cd "$(dirname "$0")" && pwd)"     # .../WiiU-PlanetBlupi/wiiu
PROJDIR="$(dirname "$SRCDIR")"              # .../WiiU-PlanetBlupi (holds planetblupi-master/ too)
BUILDDIR="${BUILDDIR:-build-wiiu}"
JOBS="${JOBS:-8}"
ACTION="${1:-build}"

# The build globs the pristine upstream sources from ../planetblupi-master, so
# the whole project root must be visible inside the container, not just wiiu/.
WIIU=/proj/wiiu

# MSYS_NO_PATHCONV stops Git-Bash-for-Windows from mangling the -v mount path.
export MSYS_NO_PATHCONV=1

case "$ACTION" in
  clean)
    rm -rf "$SRCDIR/$BUILDDIR"
    echo "Cleaned $BUILDDIR/"
    exit 0
    ;;
  shell)
    exec "$DOCKER" run --rm -it -v "$PROJDIR:/proj" -w "$WIIU" "$IMAGE" bash
    ;;
  configure|build)
    ;;
  *)
    echo "Unknown action: $ACTION (expected: configure | build | clean | shell)"
    exit 1
    ;;
esac

CONFIGURE_CMD='cd '"$WIIU"' && cmake -B '"$BUILDDIR"' \
    -DCMAKE_TOOLCHAIN_FILE='"$WIIU"'/cmake/wiiu-devkitpro.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -G "Unix Makefiles"'

BUILD_CMD='cd '"$WIIU"'/'"$BUILDDIR"' && make -j'"$JOBS"' && \
    echo "----" && ls -la '"$WIIU"'/'"$BUILDDIR"'/planetblupi.wuhb 2>/dev/null && \
    echo "OK -> '"$BUILDDIR"'/planetblupi.wuhb"'

if [ "$ACTION" = "configure" ]; then
  SCRIPT="$CONFIGURE_CMD"
else
  SCRIPT="$CONFIGURE_CMD && $BUILD_CMD"
fi

exec "$DOCKER" run --rm -v "$PROJDIR:/proj" -w "$WIIU" "$IMAGE" bash -c "$SCRIPT"
