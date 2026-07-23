# Changelog

All notable changes to the **Wii U port** of Planet Blupi. This tracks the
porting work only; for the game's own history see the upstream project.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com).

## [Unreleased] — first playable

Port of Planet Blupi to the Nintendo Wii U (Aroma), based on upstream
**v1.15.1**. Playable and verified on real hardware. Every change to the
upstream game code is guarded by `#ifdef __WIIU__`; everything else lives under
`wiiu/`.

### Added

- **Wii U target**: out-of-tree CMake build (`wiiu/`) compiling the upstream
  SDL2 sources for devkitPPC/WUT inside the `devkitpro/devkitppc` Docker image,
  producing a self-contained `.wuhb` with all freeware assets in the romfs.
- **GamePad touchscreen as mouse**: direct VPAD read
  (`VPADGetTPCalibratedPointEx`, 854×480) synthesised into SDL mouse events —
  the devkitPro SDL2 backend delivers no touch/finger events on its own.
- **Left analog stick** map scrolling (reuses the engine's keyboard-scroll
  path; the right hand stays free for the stylus).
- **FMV cutscenes**: a self-contained Cinepak decoder (`wiiu/src/cinepak.c`)
  plus an `.avi`/Ogg backend (`wiiu/src/movie_wiiu.cxx`); all clips are
  offline-transcoded to a single codec so only one decoder is needed on-device.
- Self-paced frame loop for the wiiu SDL2 backend (the upstream `SDL_AddTimer`
  tick does not fire there).

### Fixed

- **Boot / save paths**: `SDL_GetPrefPath` returns null on wiiu; user and save
  files are routed to `/vol/external01/wiiu/apps/planetblupi/`. Save + load
  verified on hardware.
- **Endianness**: level, demo and save `.blp` files are little-endian on disk;
  added a portable `SDL_SwapLE*` byte-swap layer (no-op on desktop) so they
  load and round-trip correctly on the big-endian PowerPC.
- **TTF menu text**: worked around a glyph-cache teardown crash on the
  GX2/libstdc++ combo (render per frame), and a follow-on GPU use-after-free
  that scrambled multi-line labels (defer texture frees by one frame).
- **1997 attract-mode demos** now replay correctly. Four stacked causes were
  fixed: (1) replay switched to the original 640×480 geometry; (2) legacy
  gameplay bugs re-enabled during replay to match the recordings; (3) the
  port's stick-scroll no longer overwrites the demo's recorded scroll events;
  (4) Blupi selection no longer relies on GPU pixel readback (broken on GX2) —
  which **also fixed tapping a Blupi's head to select it during normal play**.

### Notes

- English-only: gettext/libiconv are stubbed (`wiiu/compat/libintl.h`).
- Right-click is not yet mapped to a button (planned: ZL/ZR). Context actions
  remain reachable through the on-screen menu.

---

## Credits (port)

The game — all of its design, code, art, music, voices and missions — is the
work of **Daniel Roux / Epsitec SA** and the 1997 team, with the modern
open-source SDL2 version by **Mathieu Schroeter**. See
[`planetblupi-master/CREDITS`](planetblupi-master/CREDITS) and the root
[`README`](README.md).

This Wii U port was made by **Samuele Voltan** (`vs-sr-dev`), with substantial
AI assistance. Because the upstream project does not accept AI-generated code,
the port is kept as a separate repository and is never proposed for
contribution upstream. It exists so Wii U owners can enjoy a game its authors
generously made free software.
