/*
 * Planet Blupi — Wii U port
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Samuele Voltan
 *
 * No-op CMovie backend for the Wii U build.
 *
 * The desktop movie.cxx decodes the .mkv intro/cutscenes through
 * SDL_kitchensink (ffmpeg), which has no devkitPPC port. Rather than drag
 * ffmpeg onto the console, we compile this stub in movie.cxx's place: every
 * entry point reports "no movie", so the engine simply skips FMV playback
 * and continues (the intro/cutscenes are eye-candy, not game logic).
 *
 * FMV is queued, not abandoned: the plan is libsmacker + converted .smk
 * assets, exactly as proven on the LBA2 Wii U port. Keeping the interface
 * identical here means dropping the real backend in later is a 1-file swap.
 */

#include <SDL.h>   // movie.h references Sint32 / SDL_RWops / SDL_AudioDeviceID

#include "movie.h"

CMovie::CMovie (CPixmap * pixmap)
  : pixmap (pixmap)
  , m_movie (nullptr)
  , m_player (nullptr)
  , m_videoTex (nullptr)
  , backTexture (nullptr)
  , m_ret (0)
  , m_audioDev (0)
  , rw_ops (nullptr)
  , m_bEnable (false)
  , starting (false)
  , m_fPlaying (false)
  , m_fMovieOpen (false)
{
}

CMovie::~CMovie () = default;

// Returns true so DoInit()'s "New movie" bring-up succeeds; GetEnable() below
// still advertises that no movie will ever play.
bool
CMovie::Create ()
{
  m_bEnable = false;
  return true;
}

bool
CMovie::GetEnable ()
{
  return false;
}

bool
CMovie::IsExist (const std::string &)
{
  return false;
}

bool
CMovie::Play (const std::string &)
{
  return false;
}

void
CMovie::Stop ()
{
}

void
CMovie::Pause ()
{
}

void
CMovie::Resume ()
{
}

bool
CMovie::Render ()
{
  return false;
}

// Protected helpers — never reached while Play() refuses, but defined so the
// vtable/link is complete.
void
CMovie::playMovie ()
{
}

bool
CMovie::fileOpenMovie (const std::string &)
{
  return false;
}

void
CMovie::fileCloseMovie ()
{
}

void
CMovie::termAVI ()
{
}

bool
CMovie::initAVI ()
{
  return false;
}
