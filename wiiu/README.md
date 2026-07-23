# `wiiu/` — the Wii U port tree

Everything specific to the Nintendo Wii U lives here. The upstream game code
under [`../planetblupi-master/`](../planetblupi-master/) is kept pristine; the
only edits made there are small, `#ifdef __WIIU__`-guarded blocks, so upstream
stays easy to diff.

This tree is licensed under **GPLv3+**, same as the rest of the repository —
see the root [`LICENSE`](../LICENSE).

## Layout

| Path                          | Purpose                                                                 |
| ----------------------------- | ----------------------------------------------------------------------- |
| `build-wiiu.sh`               | Docker build wrapper (devkitPPC/WUT); `configure` / `clean` / `shell`   |
| `CMakeLists.txt`              | Out-of-tree build: globs `../planetblupi-master/src/*.cxx` for devkitPPC |
| `cmake/wiiu-devkitpro.cmake`  | Toolchain file                                                          |
| `compat/libintl.h`            | Stub for gettext/libintl (the port is English-only)                     |
| `src/platform_wiiu.cxx`       | Frame loop + **VPAD touchscreen → SDL mouse** + left-stick map scroll   |
| `src/movie_wiiu.cxx`          | FMV backend (demux + playback), replaces `movie_stub.cxx`               |
| `src/cinepak.c` / `.h`        | Self-contained Cinepak ("cvid") video decoder                           |
| `src/movie_stub.cxx`          | No-op FMV backend (kept for reference; not compiled)                     |
| `assets/movie/`               | FMV transcoded to Cinepak `.avi` + Ogg `.ogg`, embedded in the romfs     |

## How the build works

`build-wiiu.sh` mounts the whole project into the official
`devkitpro/devkitppc` Docker image and runs CMake + make there, so the host
only needs Docker and bash. The `wiiu/CMakeLists.txt` compiles the upstream
`.cxx` sources together with the `src/` backends above and links the wiiu
SDL2 / SDL2_image / SDL2_mixer / SDL2_ttf portlibs, producing a **self-contained**
`build-wiiu/planetblupi.wuhb` with all freeware assets baked into the romfs.

```sh
./build-wiiu.sh            # configure + build + package
./build-wiiu.sh configure  # configure only
./build-wiiu.sh clean      # rm -rf build-wiiu/
./build-wiiu.sh shell      # interactive container shell
```

Environment overrides: `DOCKER`, `IMAGE`, `BUILDDIR`, `JOBS` (see the script
header).

## Notable technical points

These are the non-obvious things that made the port work; the code comments at
each site go into more detail.

- **Touch input is read directly from VPAD.** The devkitPro SDL2 wiiu backend
  does not deliver the GamePad touchscreen as SDL mouse or finger events, so
  `platform_wiiu.cxx` reads `VPADGetTPCalibratedPointEx` (in `VPAD_TP_854X480`
  space) and synthesises SDL mouse events itself. VPAD is polled once per frame
  tick (polling every loop spin is very slow under emulation).

- **Frame loop is self-paced.** The upstream loop is driven by an
  `SDL_AddTimer` callback that does not fire on this backend, so the wiiu
  `Platform::run` pushes `EV_UPDATE` manually from `SDL_GetTicks`.

- **Big-endian save format.** The `.blp` level/demo/save files are little-endian
  on disk; on the PowerPC Wii U every multi-byte header/field is byte-swapped
  via `SDL_SwapLE*` after reading and before writing (see `decio.cxx` and the
  demo helpers in `event.cxx`, both upstream files under `#ifdef __WIIU__`).

- **TTF glyph cache disabled on wiiu.** A `shared_ptr`/`unordered_map` teardown
  crashed on this GX2/libstdc++ combo, so text is rendered per frame and the
  textures freed with a one-frame defer (GX2 is async — freeing immediately
  caused a use-after-free glitch on multi-line labels).

- **FMV is offline-transcoded.** The desktop uses SDL_kitchensink/ffmpeg on
  `.mkv` (Cinepak + MSVideo1 + Vorbis). All clips are transcoded to a single
  codec (Cinepak `.avi` + separate Ogg) so only one small decoder
  (`cinepak.c`) is needed on-device.

- **No GPU pixel readback.** `SDL_RenderReadPixels` from a render target does
  not work on the GX2 backend (returns 0). Blupi hit-testing was fixed to
  sample the source surface kept in RAM instead — this also fixed selecting a
  Blupi by tapping its head during normal play.
