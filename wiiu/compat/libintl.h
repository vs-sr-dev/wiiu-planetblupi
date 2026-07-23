/*
 * Planet Blupi — Wii U port
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Samuele Voltan
 *
 * Minimal <libintl.h> stub for the devkitPPC / WUT toolchain, which ships no
 * GNU gettext / libintl. Planet Blupi's sources include <libintl.h>
 * unconditionally (blupi.h, src/gettext.h) and sprinkle gettext(...) around
 * every user-facing string.
 *
 * For the Wii U build we run English-only: every call here is a pass-through,
 * so gettext(x) == x and the domain-binding calls are inert. This header is
 * placed first on the include path (see CMakeLists.txt) so it wins over any
 * system header search.
 *
 * NLS can be revisited later by shipping compiled .mo catalogs and a real
 * loader; nothing in the engine assumes translation actually happens.
 */

#pragma once

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline char *
gettext (const char * __msgid)
{
  return (char *) __msgid;
}

static inline char *
dgettext (const char * __domainname, const char * __msgid)
{
  (void) __domainname;
  return (char *) __msgid;
}

static inline char *
dcgettext (
  const char * __domainname, const char * __msgid, int __category)
{
  (void) __domainname;
  (void) __category;
  return (char *) __msgid;
}

static inline char *
ngettext (const char * __msgid1, const char * __msgid2, unsigned long int __n)
{
  return (char *) (__n == 1 ? __msgid1 : __msgid2);
}

static inline char *
dngettext (
  const char * __domainname, const char * __msgid1, const char * __msgid2,
  unsigned long int __n)
{
  (void) __domainname;
  return (char *) (__n == 1 ? __msgid1 : __msgid2);
}

static inline char *
textdomain (const char * __domainname)
{
  return (char *) __domainname;
}

static inline char *
bindtextdomain (const char * __domainname, const char * __dirname)
{
  (void) __domainname;
  return (char *) __dirname;
}

static inline char *
bind_textdomain_codeset (const char * __domainname, const char * __codeset)
{
  (void) __domainname;
  return (char *) __codeset;
}

#ifdef __cplusplus
}
#endif
