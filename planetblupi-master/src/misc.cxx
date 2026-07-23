/*
 * This file is part of the planetblupi source code
 * Copyright (C) 1997, Daniel Roux & EPSITEC SA
 * Copyright (C) 2017-2018, Mathieu Schroeter
 * https://epsitec.ch; https://www.blupi.org; https://github.com/blupi-games
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://gnu.org/licenses
 */

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(a, b) _mkdir (a)
#else /* _WIN32 */
#include <sys/stat.h>
#endif /*! _WIN32 */

#include <SDL_log.h>
#include <SDL_mouse.h>

#include "blupi.h"
#include "config.h"
#include "def.h"
#include "misc.h"

// Affiche un message de debug.

void
OutputDebug (const char * pMessage)
{
  SDL_LogDebug (SDL_LOG_CATEGORY_APPLICATION, "%s", pMessage);
}

// Conversion de la position de la souris.

Point
ConvLongToPos (LParam lParam)
{
  Point pos;

  pos.x = LOWORD (lParam); // horizontal position of cursor
  pos.y = HIWORD (lParam); // vertical position of cursor
  return pos;
}

static int g_seed;

/* Initialize the Microsoft pseudo-random generator */
void
InitRandom ()
{
  g_seed = 1;
  // srand (1);
}

/* We are not using rand from stdlib because on Linux the pseudo-generator
 * is using an other algorithm. Then the behaviour is not the same on all
 * platforms.
 * See http://stackoverflow.com/a/1280765/842097
 */
static int
ms_rand ()
{
  g_seed = g_seed * 0x343fd + 0x269EC3;
  return (g_seed >> 0x10) & 0x7FFF;
}

/* Returns a random number between two values (included). */
Sint32
Random (Sint32 min, Sint32 max)
{
  Sint32 n;

  n = ms_rand (); // replace rand ();
  n = min + (n % (max - min + 1));

  return (Sint32) n;
}

std::string
GetLocale ()
{
  return gettext ("en");
}

bool
IsRightReading ()
{
  return GetLocale () == "he" || GetLocale () == "ar";
}

// Retourne le nom de dossier en cours.

std::string
GetBaseDir ()
{
  return GetShareDir () + "planetblupi/";
}

static std::string
GetDataDir ()
{
#ifdef __WIIU__
  /* The .wuhb romfs is mounted read-only at /vol/content; the entire runtime
     tree lives under share/planetblupi/ there (see wiiu/CMakeLists.txt), so
     GetShareDir()/GetBaseDir() resolve without the desktop bin/.. dance. */
  return "/vol/content/";
#endif /* __WIIU__ */

  static std::string basePath;

  if (!basePath.size ())
  {
    auto sdlBasePath = SDL_GetBasePath ();

    sdlBasePath[strlen (sdlBasePath) - 1] = '\0';

    basePath = sdlBasePath;

    std::replace (basePath.begin (), basePath.end (), '\\', '/');
    basePath = basePath.substr (0, basePath.find_last_of ("//") + 1);
    SDL_free (sdlBasePath);
  }

  return basePath;
}

std::string
GetShareDir ()
{
  return GetDataDir () + "share/";
}

std::string
GetBinDir ()
{
  std::string abs;
  std::string bin = GetDataDir () + PB_BINDIR + "/";
  return bin;
}

// Ajoute le chemin permettant de lire un fichier
// utilisateur.

void
AddUserPath (std::string & pFilename)
{
#ifdef __WIIU__
  /* SDL_GetPrefPath is unusable on the wiiu SDL2 backend: it returns nullptr,
     and the desktop code below does `std::string path = temp;` which then
     constructs a std::string from a null pointer — that was the boot crash in
     ReadInfo() ("info.blp"), freezing on the Epsitec logo. Route user/save
     files to a fixed writable location on the SD card and create the parent
     directory chain best-effort. If the SD is not writable the reads simply
     miss and the engine falls back to defaults (no crash). */
  {
    const std::string base = "/vol/external01/wiiu/apps/planetblupi/";
    std::string       full = base + pFilename;
    const size_t      slash = full.find_last_of ('/');
    for (size_t p = full.find ('/', 1); p != std::string::npos && p <= slash;
         p = full.find ('/', p + 1))
      mkdir (full.substr (0, p).c_str (), 0755);
    pFilename = full;
    return;
  }
#endif /* __WIIU__ */

  const char * pText;
  size_t       pos;
  char         last;

  char *      temp = SDL_GetPrefPath ("Epsitec SA", "Planet Blupi");
  std::string path = temp;

  pText = strstr (pFilename.c_str (), "/");
  if (pText != nullptr)
  {
    pos = path.size () + (pText - pFilename.c_str ()) + 1;
    path += pFilename;
    last      = path[pos];
    path[pos] = 0;
    mkdir (path.c_str (), 0755);
    path[pos] = last;
  }
  else
    path += pFilename;

  pFilename = path;
  SDL_free (temp);
}

bool
FileExists (
  const std::string & filename, std::string & absolute, enum Location location)
{
  absolute = filename;
  FILE * file;

  switch (location)
  {
  case LOCATION_BASE:
    absolute = GetBaseDir () + filename;
    break;

  case LOCATION_USER:
    AddUserPath (absolute);
    break;

  default:
  case LOCATION_ABSOLUTE:
    break;
  }

  file = fopen (absolute.c_str (), "rb");
  if (file == nullptr)
    return false;

  fclose (file);
  return true;
}
