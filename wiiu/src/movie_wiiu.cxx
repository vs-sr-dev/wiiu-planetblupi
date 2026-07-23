/*
 * Planet Blupi — Wii U port
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Samuele Voltan
 *
 * CMovie backend for the Wii U build.
 *
 * The desktop movie.cxx decodes the .mkv cutscenes through SDL_kitchensink
 * (ffmpeg), which has no devkitPPC port. Instead, the movies are transcoded
 * offline to Cinepak video in a plain AVI container plus a separate Ogg/Vorbis
 * audio track (see wiiu/assets/movie, embedded in the romfs). Here we:
 *   - read the .avi fully into memory and demux the '00dc' video chunks,
 *   - decode each Cinepak frame to RGBA with the self-contained cinepak.c,
 *   - play the .ogg soundtrack through SDL_mixer (already initialised by the
 *     game's sound engine; the game music is stopped during a cutscene),
 *   - pace the video by wall clock so it stays in sync with the audio.
 *
 * The engine drives playback by re-posting EV_MOVIE_PLAY: each Render() shows
 * the current frame and returns true until the clip ends (then false, which
 * makes the engine call StopMovie()).
 */

#include <SDL.h>
#include <SDL_mixer.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "blupi.h"
#include "def.h"
#include "display.h"
#include "event.h"
#include "misc.h"
#include "movie.h"

#include "cinepak.h"

namespace
{
struct AviFrame
{
  uint32_t off;
  uint32_t size;
};

static uint32_t
rd32le (const uint8_t * p)
{
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
         ((uint32_t) p[3] << 24);
}
} // namespace

struct WiiUMovieState
{
  std::vector<uint8_t>  data;   // whole .avi in memory
  std::vector<AviFrame> frames; // ordered video chunks
  int                   w = 0, h = 0;
  double                fps        = 12.0;
  CinepakDecoder *      dec        = nullptr;
  std::vector<uint8_t>  rgba;         // w*h*4 decode target
  size_t                decoded    = 0; // number of frames decoded so far
  Uint32                startTicks = 0; // 0 until the first Render()
  bool                  ended      = false;
  Mix_Music *           audio      = nullptr;
};

// Demux the in-memory AVI: fill w/h/fps and the ordered list of video chunks.
static bool
ParseAvi (WiiUMovieState * m)
{
  const uint8_t * d = m->data.data ();
  const size_t    n = m->data.size ();

  if (n < 12 || memcmp (d, "RIFF", 4) != 0 || memcmp (d + 8, "AVI ", 4) != 0)
    return false;

  uint32_t microPerFrame = 0;
  size_t   p             = 12;

  while (p + 8 <= n)
  {
    const uint8_t * c  = d + p;
    const uint32_t  sz = rd32le (c + 4);

    if (memcmp (c, "LIST", 4) == 0 && p + 12 <= n)
    {
      const uint8_t * lt = c + 8;

      if (memcmp (lt, "hdrl", 4) == 0)
      {
        // Scan hdrl (descending into the nested strl LIST) for avih + strf.
        size_t hp   = p + 12;
        size_t hend = p + 8 + sz;
        while (hp + 8 <= hend && hp + 8 <= n)
        {
          const uint8_t * hc  = d + hp;
          const uint32_t  hsz = rd32le (hc + 4);
          if (memcmp (hc, "avih", 4) == 0)
            microPerFrame = rd32le (hc + 8);
          else if (memcmp (hc, "strf", 4) == 0)
          {
            const int bw = (int) rd32le (hc + 8 + 4);  // biWidth
            const int bh = (int) rd32le (hc + 8 + 8);  // biHeight
            m->w         = bw < 0 ? -bw : bw;
            m->h         = bh < 0 ? -bh : bh;
          }
          if (memcmp (hc, "LIST", 4) == 0) // descend into strl
          {
            hp += 12;
            continue;
          }
          hp += 8 + hsz + (hsz & 1);
        }
      }
      else if (memcmp (lt, "movi", 4) == 0)
      {
        size_t mp   = p + 12;
        size_t mend = p + 8 + sz;
        while (mp + 8 <= mend && mp + 8 <= n)
        {
          const uint8_t * mc  = d + mp;
          const uint32_t  msz = rd32le (mc + 4);
          if (memcmp (mc, "LIST", 4) == 0 || memcmp (mc, "rec ", 4) == 0)
          {
            mp += 12; // descend into a rec/LIST wrapper
            continue;
          }
          // Video data chunks: "00dc" (compressed) / "00db" (uncompressed).
          if (mc[0] == '0' && mc[1] == '0' &&
              (memcmp (mc + 2, "dc", 2) == 0 || memcmp (mc + 2, "db", 2) == 0))
          {
            if (msz > 0 && mp + 8 + msz <= n)
              m->frames.push_back ({(uint32_t) (mp + 8), msz});
          }
          mp += 8 + msz + (msz & 1);
        }
      }
    }

    p += 8 + sz + (sz & 1);
  }

  if (microPerFrame == 0)
    microPerFrame = 83000; // ~12 fps fallback
  m->fps = 1000000.0 / (double) microPerFrame;
  if (m->w <= 0 || m->h <= 0)
  {
    m->w = 320;
    m->h = 240;
  }
  return !m->frames.empty ();
}

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
  , m_wiiu (nullptr)
{
}

CMovie::~CMovie ()
{
  Stop ();
}

bool
CMovie::Create ()
{
  m_bEnable = true; // FMV is available on the Wii U build
  return true;
}

bool
CMovie::GetEnable ()
{
  return m_bEnable;
}

// Map the engine's "movie/name.mkv" onto the shipped "movie/name.avi".
static std::string
AviPath (const std::string & pFilename)
{
  std::string base = pFilename;
  const auto  dot  = base.rfind (".mkv");
  if (dot != std::string::npos)
    base.erase (dot);
  return GetBaseDir () + base + ".avi";
}

static std::string
OggPath (const std::string & pFilename)
{
  std::string base = pFilename;
  const auto  dot  = base.rfind (".mkv");
  if (dot != std::string::npos)
    base.erase (dot);
  return GetBaseDir () + base + ".ogg";
}

bool
CMovie::IsExist (const std::string & pFilename)
{
  SDL_RWops * rw = SDL_RWFromFile (AviPath (pFilename).c_str (), "rb");
  if (!rw)
    return false;
  SDL_RWclose (rw);
  return true;
}

bool
CMovie::fileOpenMovie (const std::string & pFilename)
{
  if (m_fMovieOpen)
    fileCloseMovie ();

  auto * m = new WiiUMovieState ();

  SDL_RWops * rw = SDL_RWFromFile (AviPath (pFilename).c_str (), "rb");
  if (!rw)
  {
    delete m;
    return false;
  }
  const Sint64 sz = SDL_RWsize (rw);
  if (sz <= 0)
  {
    SDL_RWclose (rw);
    delete m;
    return false;
  }
  m->data.resize ((size_t) sz);
  SDL_RWread (rw, m->data.data (), 1, (size_t) sz);
  SDL_RWclose (rw);

  if (!ParseAvi (m))
  {
    delete m;
    return false;
  }

  m->dec = cinepak_open (m->w, m->h);
  if (!m->dec)
  {
    delete m;
    return false;
  }
  m->rgba.assign ((size_t) m->w * m->h * 4, 0);

  m_videoTex = SDL_CreateTexture (
    g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, m->w,
    m->h);
  if (!m_videoTex)
  {
    cinepak_close (m->dec);
    delete m;
    return false;
  }

  // Prime the first frame so there is always something to show immediately.
  if (!m->frames.empty ())
  {
    const AviFrame & f0 = m->frames[0];
    cinepak_decode (
      m->dec, m->data.data () + f0.off, f0.size, m->rgba.data (), m->w * 4);
    m->decoded = 1;
    SDL_UpdateTexture (m_videoTex, nullptr, m->rgba.data (), m->w * 4);
  }

  // Optional soundtrack (silent if absent). Started on the first Render() so
  // it lines up with the video clock.
  m->audio = Mix_LoadMUS (OggPath (pFilename).c_str ());

  m_wiiu       = m;
  m_fMovieOpen = true;
  return true;
}

void
CMovie::fileCloseMovie ()
{
  m_fPlaying   = false;
  m_fMovieOpen = false;

  if (m_videoTex)
  {
    SDL_DestroyTexture (m_videoTex);
    m_videoTex = nullptr;
  }

  if (m_wiiu)
  {
    if (m_wiiu->audio)
    {
      Mix_HaltMusic ();
      Mix_FreeMusic (m_wiiu->audio);
    }
    if (m_wiiu->dec)
      cinepak_close (m_wiiu->dec);
    delete m_wiiu;
    m_wiiu = nullptr;
  }
}

void
CMovie::playMovie ()
{
  m_fPlaying = true;
  if (m_wiiu)
    m_wiiu->startTicks = 0; // set on the first Render()
}

bool
CMovie::Play (const std::string & pFilename)
{
  if (!m_bEnable)
    return false;

  if (!fileOpenMovie (pFilename))
    return false;

  playMovie ();
  CEvent::PushUserEvent (EV_MOVIE_PLAY);
  return true;
}

void
CMovie::Stop ()
{
  if (m_fMovieOpen)
    fileCloseMovie ();
}

void
CMovie::Pause ()
{
  if (m_wiiu && m_fPlaying)
    Mix_PauseMusic ();
}

void
CMovie::Resume ()
{
  if (m_wiiu && m_fPlaying)
    Mix_ResumeMusic ();
}

bool
CMovie::Render ()
{
  if (!m_bEnable || !m_fPlaying || !m_wiiu)
    return false;

  WiiUMovieState * m = m_wiiu;
  if (m->ended || m->frames.empty ())
    return false;

  const Uint32 now = SDL_GetTicks ();
  if (m->startTicks == 0)
  {
    m->startTicks = now == 0 ? 1 : now;
    if (m->audio)
      Mix_PlayMusic (m->audio, 0); // start audio with the video clock
  }

  const double elapsed = (double) (now - m->startTicks) / 1000.0;
  size_t       target  = (size_t) (elapsed * m->fps);
  if (target > m->frames.size ())
    target = m->frames.size ();

  // Decode forward to the target frame (Cinepak deltas must run in order).
  bool newFrame = false;
  while (m->decoded < m->frames.size () && m->decoded < target)
  {
    const AviFrame & fr = m->frames[m->decoded];
    cinepak_decode (
      m->dec, m->data.data () + fr.off, fr.size, m->rgba.data (), m->w * 4);
    m->decoded++;
    newFrame = true;
  }
  if (newFrame)
    SDL_UpdateTexture (m_videoTex, nullptr, m->rgba.data (), m->w * 4);

  // Finished: all frames shown and the clip's duration has elapsed.
  if (m->decoded >= m->frames.size () &&
      elapsed >= (double) m->frames.size () / m->fps)
  {
    m->ended = true;
    return false;
  }

  SDL_SetRenderTarget (g_renderer, nullptr);
  SDL_SetRenderDrawColor (g_renderer, 0, 0, 0, 255);
  SDL_RenderClear (g_renderer);

  SDL_Rect dst;
  dst.x = (LXIMAGE () - LXLOGIC ()) / 2;
  dst.y = 0;
  dst.w = LXLOGIC ();
  dst.h = LYLOGIC ();
  SDL_RenderCopy (g_renderer, m_videoTex, nullptr, &dst);

  SDL_RenderPresent (g_renderer);
  CEvent::PushUserEvent (EV_MOVIE_PLAY);
  return true;
}

// Never used on the Wii U path (initAVI/termAVI belonged to kitchensink) but
// defined to keep the CMovie vtable/link complete.
bool
CMovie::initAVI ()
{
  return true;
}

void
CMovie::termAVI ()
{
}
