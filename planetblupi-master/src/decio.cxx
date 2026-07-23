/*
 * This file is part of the planetblupi source code
 * Copyright (C) 1997, Daniel Roux & EPSITEC SA
 * Copyright (C) 2017, Mathieu Schroeter
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

#include <SDL_endian.h>

#include "decor.h"
#include "def.h"
#include "misc.h"

typedef struct {
  Sint16 majRev;
  Sint16 minRev;
  Sint32 nbDecor;
  Sint32 lgDecor;
  Sint32 nbBlupi;
  Sint32 lgBlupi;
  Sint32 nbMove;
  Sint32 lgMove;
  Sint16 reserve1[100];
  Point  celCoin;
  Sint16 world;
  Sint32 time;
  char   buttonExist[MAXBUTTON];
  Term   term;
  Sint16 music;
  Sint16 region;
  Sint32 totalTime;
  Sint16 skill;
  Point  memoPos[4];
  Sint16 reserve2[29];
} DescFile;

typedef struct {
  Sint32 bExist; // true -> utilisé
  Sint32 bHili;  // true -> sélectionné

  Sint16 perso; // personnage, voir (*)

  Sint16 goalAction; // action (Sint32 terme)
  Sint16 goalPhase;  // phase (Sint32 terme)
  Point  goalCel;    // cellule visée (Sint32 terme)
  Point  passCel;    // cellule tranversante

  Sint16 energy; // énergie restante

  Point  cel;     // cellule actuelle
  Point  destCel; // cellule destination
  Sint16 action;  // action en cours
  Sint16 aDirect; // direction actuelle
  Sint16 sDirect; // direction souhaitée

  Point  pos;  // position relative à partir de la cellule
  Sint16 posZ; // déplacement z
  Sint16 channel;
  Sint16 lastIcon;
  Sint16 icon;
  Sint16 phase;     // phase dans l'action
  Sint16 step;      // pas global
  Sint16 interrupt; // 0=prioritaire, 1=normal, 2=misc
  Sint16 clipLeft;

  Sint32 nbUsed; // nb de points déjà visités
  char   nextRankUsed;
  Point  posUsed[MAXUSED];
  char   rankUsed[MAXUSED];

  Sint16 takeChannel; // objet transporté
  Sint16 takeIcon;

  Point fix; // point fixe (cultive, pont)

  Sint16 jaugePhase;
  Sint16 jaugeMax;
  Sint16 stop;     // 1 -> devra stopper
  Sint16 bArrow;   // true -> flèche en dessus de blupi
  Sint16 bRepeat;  // true -> répète l'action
  Sint16 nLoop;    // nb de boucles pour GOAL_OTHERLOOP
  Sint16 cLoop;    // boucle en cours
  Sint16 vIcon;    // icône variable
  Point  goalHili; // but visé
  Sint16 bMalade;  // true -> blupi malade
  Sint16 bCache;   // true -> caché (pas dessiné)
  Sint16 vehicule; // véhicule utilisé par blupi, voir (**)
  char   busyCount;
  char   busyDelay;
  char   clicCount;
  char   clicDelay;
  char   reserve2[2];
} OldBlupi;

/* -------------------------------------------------------------------------
 * Endianness layer for the .blp save files.
 *
 * The .blp format is little-endian on disk (it was authored on x86). On a
 * big-endian host — the Wii U's PowerPC — every multi-byte field must be
 * swapped after reading and before writing, or the header validation in
 * Read()/FileExist() fails and the engine falls back to the "insert CD-Rom"
 * screen. SDL_SwapLE16/32 compile to no-ops on little-endian, so these
 * helpers are completely inert on desktop builds and need no #ifdef.
 * ------------------------------------------------------------------------- */
static inline void
SwapLE (Sint16 & v)
{
  v = (Sint16) SDL_SwapLE16 ((Uint16) v);
}

static inline void
SwapLE (Sint32 & v)
{
  v = (Sint32) SDL_SwapLE32 ((Uint32) v);
}

static inline void
SwapLE (Point & p)
{
  SwapLE (p.x);
  SwapLE (p.y);
}

static void
SwapLE (Cellule & c)
{
  SwapLE (c.floorChannel);
  SwapLE (c.floorIcon);
  SwapLE (c.objectChannel);
  SwapLE (c.objectIcon);
  SwapLE (c.fog);
  SwapLE (c.rankMove);
  SwapLE (c.workBlupi);
  SwapLE (c.fire);
}

static void
SwapLE (Term & t)
{
  SwapLE (t.bHachBlupi);
  SwapLE (t.bHachPlanche);
  SwapLE (t.bStopFire);
  SwapLE (t.nbMinBlupi);
  SwapLE (t.nbMaxBlupi);
  SwapLE (t.bHomeBlupi);
  SwapLE (t.bKillRobots);
  SwapLE (t.bHachTomate);
  SwapLE (t.bHachMetal);
  SwapLE (t.bHachRobot);
  for (size_t i = 0; i < countof (t.reserve); ++i)
    SwapLE (t.reserve[i]);
}

static void
SwapLE (Move & m)
{
  SwapLE (m.bExist);
  SwapLE (m.cel);
  SwapLE (m.rankBlupi);
  SwapLE (m.bFloor);
  SwapLE (m.channel);
  SwapLE (m.icon);
  SwapLE (m.maskChannel);
  SwapLE (m.maskIcon);
  SwapLE (m.phase);
  SwapLE (m.rankMoves);
  SwapLE (m.rankIcons);
  SwapLE (m.total);
  SwapLE (m.delai);
  SwapLE (m.stepY);
  SwapLE (m.cTotal);
  SwapLE (m.cDelai);
}

/* The animated fields shared by Blupi and OldBlupi (OldBlupi is the 1.3-era
 * prefix of Blupi, so the same leading members line up). */
template <typename T>
static void
SwapBlupiCommon (T & b)
{
  SwapLE (b.bExist);
  SwapLE (b.bHili);
  SwapLE (b.perso);
  SwapLE (b.goalAction);
  SwapLE (b.goalPhase);
  SwapLE (b.goalCel);
  SwapLE (b.passCel);
  SwapLE (b.energy);
  SwapLE (b.cel);
  SwapLE (b.destCel);
  SwapLE (b.action);
  SwapLE (b.aDirect);
  SwapLE (b.sDirect);
  SwapLE (b.pos);
  SwapLE (b.posZ);
  SwapLE (b.channel);
  SwapLE (b.lastIcon);
  SwapLE (b.icon);
  SwapLE (b.phase);
  SwapLE (b.step);
  SwapLE (b.interrupt);
  SwapLE (b.clipLeft);
  SwapLE (b.nbUsed);
  /* nextRankUsed is a char */
  for (size_t i = 0; i < countof (b.posUsed); ++i)
    SwapLE (b.posUsed[i]);
  /* rankUsed is a char[] */
  SwapLE (b.takeChannel);
  SwapLE (b.takeIcon);
  SwapLE (b.fix);
  SwapLE (b.jaugePhase);
  SwapLE (b.jaugeMax);
  SwapLE (b.stop);
  SwapLE (b.bArrow);
  SwapLE (b.bRepeat);
  SwapLE (b.nLoop);
  SwapLE (b.cLoop);
  SwapLE (b.vIcon);
  SwapLE (b.goalHili);
  SwapLE (b.bMalade);
  SwapLE (b.bCache);
  SwapLE (b.vehicule);
  /* busyCount/busyDelay/clicCount/clicDelay/reserve2 are chars */
}

static void
SwapLE (OldBlupi & b)
{
  SwapBlupiCommon (b);
}

static void
SwapLE (Blupi & b)
{
  SwapBlupiCommon (b);
  for (size_t i = 0; i < countof (b.listButton); ++i)
    SwapLE (b.listButton[i]);
  for (size_t i = 0; i < countof (b.listCel); ++i)
    SwapLE (b.listCel[i]);
  for (size_t i = 0; i < countof (b.listParam); ++i)
    SwapLE (b.listParam[i]);
  SwapLE (b.repeatLevelHope);
  SwapLE (b.repeatLevel);
  for (size_t i = 0; i < countof (b.reserve3); ++i)
    SwapLE (b.reserve3[i]);
}

static void
SwapLE (DescFile & d)
{
  SwapLE (d.majRev);
  SwapLE (d.minRev);
  SwapLE (d.nbDecor);
  SwapLE (d.lgDecor);
  SwapLE (d.nbBlupi);
  SwapLE (d.lgBlupi);
  SwapLE (d.nbMove);
  SwapLE (d.lgMove);
  for (size_t i = 0; i < countof (d.reserve1); ++i)
    SwapLE (d.reserve1[i]);
  SwapLE (d.celCoin);
  SwapLE (d.world);
  SwapLE (d.time);
  /* buttonExist is a char[] */
  SwapLE (d.term);
  SwapLE (d.music);
  SwapLE (d.region);
  SwapLE (d.totalTime);
  SwapLE (d.skill);
  for (size_t i = 0; i < countof (d.memoPos); ++i)
    SwapLE (d.memoPos[i]);
  for (size_t i = 0; i < countof (d.reserve2); ++i)
    SwapLE (d.reserve2[i]);
}

/* Swap the whole decor grid in place (its own helper because both Read and
 * Write walk the [MAXCELX/2][MAXCELY/2] array the same way). */
static void
SwapDecorLE (Cellule decor[MAXCELX / 2][MAXCELY / 2])
{
  for (Sint32 x = 0; x < MAXCELX / 2; ++x)
    for (Sint32 y = 0; y < MAXCELY / 2; ++y)
      SwapLE (decor[x][y]);
}

// Sauve le décor sur disque.

bool
CDecor::Write (Sint32 rank, bool bUser, Sint32 world, Sint32 time, Sint32 total)
{
  std::string filename;
  FILE *      file    = nullptr;
  DescFile *  pBuffer = nullptr;
  Sint32      i;
  size_t      nb;

  if (bUser)
  {
    filename = string_format ("data/user%.3d.blp", rank);
    AddUserPath (filename);
  }
  else
  {
    filename = string_format ("data/world%.3d.blp", rank);
    AddUserPath (filename);
  }

  file = fopen (filename.c_str (), "wb");
  if (file == nullptr)
    goto error;

  pBuffer = (DescFile *) malloc (sizeof (DescFile));
  if (pBuffer == nullptr)
    goto error;
  memset (pBuffer, 0, sizeof (DescFile));

  pBuffer->majRev    = 1;
  pBuffer->minRev    = 5;
  pBuffer->celCoin   = m_celCorner;
  pBuffer->world     = world;
  pBuffer->time      = time;
  pBuffer->totalTime = total;
  pBuffer->term      = m_term;
  pBuffer->music     = m_music;
  pBuffer->region    = m_region;
  pBuffer->skill     = m_skill;
  pBuffer->nbDecor   = MAXCELX * MAXCELY;
  pBuffer->lgDecor   = sizeof (Cellule);
  pBuffer->nbBlupi   = MAXBLUPI;
  pBuffer->lgBlupi   = sizeof (Blupi);
  pBuffer->nbMove    = MAXMOVE;
  pBuffer->lgMove    = sizeof (Move);

  for (i = 0; i < MAXBUTTON; i++)
    pBuffer->buttonExist[i] = m_buttonExist[i];

  for (i = 0; i < 4; i++)
    pBuffer->memoPos[i] = m_memoPos[i];

  /* Everything below is written little-endian on disk. pBuffer is a scratch
   * copy so we can swap it outright; the m_* members are live game state, so
   * we swap them in place, write, then swap back to restore host order. All
   * SwapLE calls are no-ops on little-endian hosts. */
  SwapLE (*pBuffer);
  nb = fwrite (pBuffer, sizeof (DescFile), 1, file);
  if (nb < 1)
    goto error;

  SwapDecorLE (m_decor);
  nb = fwrite (m_decor, sizeof (Cellule), MAXCELX * MAXCELY / 4, file);
  SwapDecorLE (m_decor);
  if (nb < MAXCELX * MAXCELY / 4)
    goto error;

  for (i = 0; i < MAXBLUPI; i++)
    SwapLE (m_blupi[i]);
  nb = fwrite (m_blupi, sizeof (Blupi), MAXBLUPI, file);
  for (i = 0; i < MAXBLUPI; i++)
    SwapLE (m_blupi[i]);
  if (nb < MAXBLUPI)
    goto error;

  for (i = 0; i < MAXMOVE; i++)
    SwapLE (m_move[i]);
  nb = fwrite (m_move, sizeof (Move), MAXMOVE, file);
  for (i = 0; i < MAXMOVE; i++)
    SwapLE (m_move[i]);
  if (nb < MAXMOVE)
    goto error;

  for (i = 0; i < MAXLASTDRAPEAU; i++)
    SwapLE (m_lastDrapeau[i]);
  nb = fwrite (m_lastDrapeau, sizeof (Point), MAXLASTDRAPEAU, file);
  for (i = 0; i < MAXLASTDRAPEAU; i++)
    SwapLE (m_lastDrapeau[i]);
  if (nb < MAXLASTDRAPEAU)
    goto error;

  free (pBuffer);
  fclose (file);
  return true;

error:
  if (pBuffer != nullptr)
    free (pBuffer);
  if (file != nullptr)
    fclose (file);
  return false;
}

// Lit le décor sur disque.

bool
CDecor::Read (
  Sint32 rank, bool bUser, Sint32 & world, Sint32 & time, Sint32 & total)
{
  std::string filename;
  FILE *      file    = nullptr;
  DescFile *  pBuffer = nullptr;
  Sint32      majRev, minRev;
  Sint32      i, x, y;
  size_t      nb;
  OldBlupi    oldBlupi;

  Init (-1, -1);

  if (bUser)
  {
    filename = string_format ("data/user%.3d.blp", rank);
    AddUserPath (filename);
  }
  else if (rank >= 200)
  {
    filename = string_format ("data/world%.3d.blp", rank);
    AddUserPath (filename);
  }
  else
    filename = string_format (GetBaseDir () + "data/world%.3d.blp", rank);

  file = fopen (filename.c_str (), "rb");
  if (file == nullptr)
    goto error;

  pBuffer = (DescFile *) malloc (sizeof (DescFile));
  if (pBuffer == nullptr)
    goto error;

  nb = fread (pBuffer, sizeof (DescFile), 1, file);
  if (nb < 1)
    goto error;

  SwapLE (*pBuffer); // .blp header is little-endian on disk

  majRev = pBuffer->majRev;
  minRev = pBuffer->minRev;

  if (majRev == 1 && minRev == 0)
    goto error;

  if (majRev == 1 && minRev == 3)
  {
    if (
      pBuffer->nbDecor != MAXCELX * MAXCELY ||
      pBuffer->lgDecor != sizeof (Cellule) || pBuffer->nbBlupi != MAXBLUPI ||
      pBuffer->lgBlupi != sizeof (OldBlupi) || pBuffer->nbMove != MAXMOVE ||
      pBuffer->lgMove != sizeof (Move))
      goto error;
  }
  else
  {
    if (
      pBuffer->nbDecor != MAXCELX * MAXCELY ||
      pBuffer->lgDecor != sizeof (Cellule) || pBuffer->nbBlupi != MAXBLUPI ||
      pBuffer->lgBlupi != sizeof (Blupi) || pBuffer->nbMove != MAXMOVE ||
      pBuffer->lgMove != sizeof (Move))
      goto error;
  }

  SetCorner (pBuffer->celCoin);
  if (bUser)
  {
    world = pBuffer->world;
    time  = pBuffer->time;
    total = pBuffer->totalTime;
  }
  m_celHome = pBuffer->celCoin;
  m_term    = pBuffer->term;
  m_music   = pBuffer->music;
  m_region  = pBuffer->region;

  if (bUser)
    m_skill = pBuffer->skill;

  for (i = 0; i < MAXBUTTON; i++)
    m_buttonExist[i] = pBuffer->buttonExist[i];

  for (i = 0; i < 4; i++)
    m_memoPos[i] = pBuffer->memoPos[i];

  nb = fread (m_decor, sizeof (Cellule), MAXCELX * MAXCELY / 4, file);
  if (nb < MAXCELX * MAXCELY / 4)
    goto error;
  SwapDecorLE (m_decor); // cells are little-endian on disk
  if (majRev == 1 && minRev < 5)
  {
    for (x = 0; x < MAXCELX / 2; x++)
    {
      for (y = 0; y < MAXCELY / 2; y++)
      {
        if (m_decor[x][y].objectIcon >= 128 && m_decor[x][y].objectIcon <= 130)
          m_decor[x][y].objectIcon -= 128 - 17;
      }
    }
  }

  /* Restore the flagged state where flagged ground */
  for (size_t i = 0; i < countof (m_decor); ++i)
    for (size_t j = 0; j < countof (m_decor[i]); ++j)
    {
      if (m_decor[i][j].objectIcon == 124)
        m_decorMem[i][j].flagged = true;
    }

  if (majRev == 1 && minRev == 3)
  {
    memset (m_blupi, 0, sizeof (Blupi) * MAXBLUPI);
    for (i = 0; i < MAXBLUPI; i++)
    {
      nb = fread (&oldBlupi, sizeof (OldBlupi), 1, file);
      if (nb != 1)
        goto error;
      SwapLE (oldBlupi); // little-endian on disk
      memcpy (m_blupi + i, &oldBlupi, sizeof (OldBlupi));
      ListFlush (i);
    }
  }
  else
  {
    nb = fread (m_blupi, sizeof (Blupi), MAXBLUPI, file);
    if (nb < MAXBLUPI)
      goto error;
    for (i = 0; i < MAXBLUPI; i++)
      SwapLE (m_blupi[i]); // little-endian on disk
  }

  nb = fread (m_move, sizeof (Move), MAXMOVE, file);
  if (nb < MAXMOVE)
    goto error;
  for (i = 0; i < MAXMOVE; i++)
    SwapLE (m_move[i]); // little-endian on disk

  nb = fread (m_lastDrapeau, sizeof (Point), MAXLASTDRAPEAU, file);
  if (nb < MAXLASTDRAPEAU)
    InitDrapeau ();
  else
    for (i = 0; i < MAXLASTDRAPEAU; i++)
      SwapLE (m_lastDrapeau[i]); // little-endian on disk

  BlupiDeselect (); // désélectionne tous les blupi

  free (pBuffer);
  fclose (file);
  return true;

error:
  if (pBuffer != nullptr)
    free (pBuffer);
  if (file != nullptr)
    fclose (file);

  Flush (); // initialise un décor neutre
  return false;
}

// Indique si un fichier existe sur disque.

bool
CDecor::FileExist (
  Sint32 rank, bool bUser, Sint32 & world, Sint32 & time, Sint32 & total)
{
  std::string filename;
  FILE *      file    = nullptr;
  DescFile *  pBuffer = nullptr;
  Sint32      majRev, minRev;
  size_t      nb;

  if (bUser)
  {
    filename = string_format ("data/user%.3d.blp", rank);
    AddUserPath (filename);
  }
  else if (rank >= 200)
  {
    filename = string_format ("data/world%.3d.blp", rank);
    AddUserPath (filename);
  }
  else
    filename = string_format (GetBaseDir () + "data/world%.3d.blp", rank);

  file = fopen (filename.c_str (), "rb");
  if (file == nullptr)
    goto error;

  pBuffer = (DescFile *) malloc (sizeof (DescFile));
  if (pBuffer == nullptr)
    goto error;

  nb = fread (pBuffer, sizeof (DescFile), 1, file);
  if (nb < 1)
    goto error;

  SwapLE (*pBuffer); // .blp header is little-endian on disk

  majRev = pBuffer->majRev;
  minRev = pBuffer->minRev;

  if (majRev == 1 && minRev == 0)
    goto error;

  if (majRev == 1 && minRev == 3)
  {
    if (
      pBuffer->nbDecor != MAXCELX * MAXCELY ||
      pBuffer->lgDecor != sizeof (Cellule) || pBuffer->nbBlupi != MAXBLUPI ||
      pBuffer->lgBlupi != sizeof (OldBlupi) || pBuffer->nbMove != MAXMOVE ||
      pBuffer->lgMove != sizeof (Move))
      goto error;
  }
  else
  {
    if (
      pBuffer->nbDecor != MAXCELX * MAXCELY ||
      pBuffer->lgDecor != sizeof (Cellule) || pBuffer->nbBlupi != MAXBLUPI ||
      pBuffer->lgBlupi != sizeof (Blupi) || pBuffer->nbMove != MAXMOVE ||
      pBuffer->lgMove != sizeof (Move))
      goto error;
  }

  world = pBuffer->world;
  time  = pBuffer->time;
  total = pBuffer->totalTime;

  free (pBuffer);
  fclose (file);
  return true;

error:
  if (pBuffer != nullptr)
    free (pBuffer);
  if (file != nullptr)
    fclose (file);
  return false;
}

#define MARG 18

// Initialise un décor neutre.

void
CDecor::Flush ()
{
  Sint32 x, y, i, icon;

  Init (-1, -1);

  for (x = 0; x < MAXCELX; x += 2)
  {
    for (y = 0; y < MAXCELY; y += 2)
    {
      if (x < MARG || x > MAXCELX - MARG || y < MARG || y > MAXCELY - MARG)
      {
        icon = 14; // eau
        goto put;
      }

      if (x == MARG && y == MARG)
      {
        icon = 12;
        goto put;
      }
      if (x == MAXCELX - MARG && y == MARG)
      {
        icon = 13;
        goto put;
      }
      if (x == MARG && y == MAXCELY - MARG)
      {
        icon = 11;
        goto put;
      }
      if (x == MAXCELX - MARG && y == MAXCELY - MARG)
      {
        icon = 10;
        goto put;
      }

      if (x == MARG)
      {
        icon = 4;
        goto put;
      }
      if (x == MAXCELX - MARG)
      {
        icon = 2;
        goto put;
      }
      if (y == MARG)
      {
        icon = 5;
        goto put;
      }
      if (y == MAXCELY - MARG)
      {
        icon = 3;
        goto put;
      }

      icon = 1; // terre

    put:
      m_decor[x / 2][y / 2].floorChannel = CHFLOOR;
      m_decor[x / 2][y / 2].floorIcon    = icon;
    }
  }

  for (i = 0; i < MAXBLUPI; i++)
    m_blupi[i].bExist = false;

  BlupiCreate (GetCel (102, 100), ACTION_STOP, DIRECT_S, 0, MAXENERGY);
  m_decor[98 / 2][100 / 2].floorChannel  = CHFLOOR;
  m_decor[98 / 2][100 / 2].floorIcon     = 16;
  m_decor[98 / 2][100 / 2].objectChannel = CHOBJECT;
  m_decor[98 / 2][100 / 2].objectIcon    = 113;

  for (i = 0; i < MAXBUTTON; i++)
    m_buttonExist[i] = 1;

  for (i = 0; i < 4; i++)
  {
    m_memoPos[i].x = 0;
    m_memoPos[i].y = 0;
  }

  memset (&m_term, 0, sizeof (Term));
  m_term.bHomeBlupi = true;
  m_term.nbMinBlupi = 1;
  m_term.nbMaxBlupi = 1;

  m_music  = 0;
  m_region = 0;

  m_celHome.x = 90;
  m_celHome.y = 98;
  SetCorner (m_celHome);
  InitAfterBuild ();
  LoadImages ();
}
