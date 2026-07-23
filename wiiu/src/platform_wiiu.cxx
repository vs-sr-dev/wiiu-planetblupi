/*
 * Planet Blupi — Wii U port
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Samuele Voltan
 *
 * Wii U platform backend: the main loop, plus (later) input augmentation.
 *
 * WHY THIS FILE OWNS THE LOOP
 * ---------------------------
 * The desktop backend (platform_sdl.cxx) drives the engine with an
 * SDL_AddTimer that pushes an EV_UPDATE every g_timerInterval ms, and blocks
 * the main thread in SDL_WaitEvent the rest of the time. On the devkitPro
 * wiiu SDL2 backend that timer callback never fires: the game rendered its
 * first frame (the Epsitec logo) and then sat forever in SDL_WaitEvent with
 * no events and no ticks — Cemu reported 0 fps, and IntroStep() (which
 * advances intro1 -> the menu after ~20 ticks) never ran.
 *
 * So the Wii U build runs a conventional active game loop instead: poll all
 * pending SDL events, then synthesize one EV_UPDATE per g_timerInterval using
 * SDL_GetTicks as the clock, and dispatch it straight to the same
 * HandleEvent the engine already uses. No timer thread, no blocking wait —
 * frames are produced every iteration, exactly like a console title expects.
 *
 * INPUT (touch as a mouse): contrary to the initial assumption, the devkitPro
 * SDL2 wiiu backend does NOT deliver the GamePad touchscreen as SDL mouse (or
 * finger) events — on real hardware AND in Cemu the game received zero input of
 * any kind. So we read the VPAD directly here (same approach as the LBA2 wiiu
 * port) and synthesize the SDL mouse events the engine expects. The touch is
 * calibrated to VPAD_TP_854X480, which matches Blupi's logical/game space, so
 * the coordinates feed straight into the engine's raw event.x/y hit-testing.
 * The right-stick map-pan and ZL/ZR right-click are layered on here later.
 */

#ifdef __WIIU__

#include <SDL.h>
#include <vpad/input.h>

#include "blupi.h"   // g_timerInterval, EV_* ids, g_window
#include "event.h"
#include "platform.h"

/* getType(), timer() and the static Platform::handleEvent member stay defined
   in platform_sdl.cxx (compiled unguarded on every target); only run() is
   overridden here, so we must NOT redefine them or the link double-defines. */

/* Poll the GamePad touchscreen and push the corresponding SDL mouse events.
 * Coordinates come out of VPADGetTPCalibratedPointEx in 854x480 space, i.e.
 * the engine's game space. We tag the synthetic events with windowID 0 so
 * SDL's render-logical-size event watch (which only rewrites events whose
 * windowID matches the renderer's window) leaves the coordinates untouched —
 * the engine then uses them directly (FromDisplayToGame is a no-op in
 * fullscreen). Down-edge -> MOUSEBUTTONDOWN, up-edge -> MOUSEBUTTONUP, and a
 * MOUSEMOTION while held so the software cursor tracks the finger. */
static void
WiiUPumpTouch ()
{
  static bool wasDown = false;
  static int  lastX = 0, lastY = 0;

  VPADStatus    st;
  VPADReadError err = VPAD_READ_SUCCESS;
  int           n = VPADRead (VPAD_CHAN_0, &st, 1, &err);
  if (n <= 0 || err != VPAD_READ_SUCCESS)
    return; // no fresh sample this poll: keep the previous touch state

  /* Left stick -> smooth map scroll (the DRC has no mouse-edge hover). The
     left stick keeps the right hand free for the stylus / touchscreen. */
  if (g_pEvent)
    g_pEvent->WiiUScrollStick (st.leftStick.x, st.leftStick.y);

  VPADTouchData raw = st.tpNormal; // wut wants a non-const source
  VPADTouchData cal;
  VPADGetTPCalibratedPointEx (VPAD_CHAN_0, VPAD_TP_854X480, &cal, &raw);

  const bool down = (cal.touched != 0) && (cal.validity == VPAD_VALID);

  int ex = lastX, ey = lastY;
  if (down)
  {
    ex = (int) cal.x;
    ey = (int) cal.y;
    lastX = ex;
    lastY = ey;

    SDL_Event mo;
    SDL_zero (mo);
    mo.type            = SDL_MOUSEMOTION;
    mo.motion.windowID = 0;
    mo.motion.x        = ex;
    mo.motion.y        = ey;
    SDL_PushEvent (&mo);
  }

  if (down && !wasDown)
  {
    SDL_Event bd;
    SDL_zero (bd);
    bd.type            = SDL_MOUSEBUTTONDOWN;
    bd.button.windowID = 0;
    bd.button.button   = SDL_BUTTON_LEFT;
    bd.button.state    = SDL_PRESSED;
    bd.button.clicks   = 1;
    bd.button.x        = ex;
    bd.button.y        = ey;
    SDL_PushEvent (&bd);
  }
  else if (!down && wasDown)
  {
    SDL_Event bu;
    SDL_zero (bu);
    bu.type            = SDL_MOUSEBUTTONUP;
    bu.button.windowID = 0;
    bu.button.button   = SDL_BUTTON_LEFT;
    bu.button.state    = SDL_RELEASED;
    bu.button.clicks   = 1;
    bu.button.x        = lastX;
    bu.button.y        = lastY;
    SDL_PushEvent (&bu);
  }

  wasDown = down;
}

void
Platform::run (std::function<void (const SDL_Event &)> handleEvent)
{
  SDL_Log ("WIIU: Platform::run reached — entering main loop");

  VPADInit ();

  const Uint32 interval = g_timerInterval > 0 ? (Uint32) g_timerInterval : 50;
  Uint32       last     = SDL_GetTicks ();
  bool         running  = true;

  auto pump = [&] () {
    SDL_Event event;
    while (SDL_PollEvent (&event))
    {
      handleEvent (event);
      if (event.type == SDL_QUIT)
        running = false;
    }
  };

  while (running)
  {
    const Uint32 now    = SDL_GetTicks ();
    const bool   doTick = (now - last >= interval);

    /* Poll the touchscreen only once per frame tick (g_timerInterval, ~20 Hz).
       VPADRead is comparatively expensive — especially under Cemu's HLE — and
       the engine only consumes input at the tick rate anyway, so polling it on
       every spin of the loop (which idles at ~1 kHz via SDL_Delay(1)) crushed
       the frame rate to ~1 fps for no benefit. */
    if (doTick)
      WiiUPumpTouch ();

    pump ();
    if (!running)
      break;

    if (doTick)
    {
      last = now;

      /* Same payload platform_sdl.cxx's timer used to push, delivered
         directly to the engine's HandleEvent — this is the frame tick that
         runs Update() -> IntroStep()/BlupiStep() and presents via Display(). */
      SDL_Event upd;
      SDL_zero (upd);
      upd.type       = SDL_USEREVENT;
      upd.user.code  = EV_UPDATE;
      upd.user.data1 = nullptr;
      upd.user.data2 = nullptr;
      handleEvent (upd);
    }
    else
      SDL_Delay (1);
  }
}

#endif /* __WIIU__ */
