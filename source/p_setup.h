// Emacs style mode select -*- C++ -*- vi:ts=3:sw=3:set et:
//-----------------------------------------------------------------------------
//
// Copyright(C) 2000 James Haley
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//--------------------------------------------------------------------------
//
// DESCRIPTION:
//   Setup a game, startup stuff.
//
//-----------------------------------------------------------------------------

#ifndef P_SETUP_H__
#define P_SETUP_H__

#include "doomdef.h"  // for skill_t
#include "doomtype.h" // for byte
#include "m_fixed.h"  // for fixed_t

class  Mobj;
struct seg_t;

// haleyjd 10/03/05: let P_CheckLevel determine the map format
enum
{
   LEVEL_FORMAT_INVALID,
   LEVEL_FORMAT_DOOM,
   LEVEL_FORMAT_HEXEN
};

class WadDirectory;
int P_CheckLevel(WadDirectory *dir, int lumpnum); // haleyjd: now used in d_main.c

void P_SetupLevel(WadDirectory *dir, const char *mapname, int playermask, skill_t skill);
void P_Init(void);               // Called by startup code.
void P_InitThingLists(void);

extern byte     *rejectmatrix;   // for fast sight rejection

// killough 3/1/98: change blockmap from "short" to "long" offsets:
extern int      *blockmaplump;   // offsets in blockmap are from here
extern int      *blockmap;
extern int      bmapwidth;
extern int      bmapheight;      // in mapblocks
extern fixed_t  bmaporgx;
extern fixed_t  bmaporgy;        // origin of block map
extern Mobj   **blocklinks;    // for thing chains

extern bool     newlevel;
extern int      doom1level;
extern char     levelmapname[10];

#if 0
typedef struct                          // Standard OLO stuff, put in WADs
{
  unsigned char header[3];                 // Header
  unsigned char space1;
  unsigned char extend;
  unsigned char space2;

  // Standard
  unsigned char levelwarp;
  unsigned char lastlevel;
  unsigned char deathmatch;
  unsigned char skill_level;
  unsigned char nomonsters;
  unsigned char respawn;
  unsigned char fast;

  unsigned char levelname[32][32];
} olo_t;

extern olo_t olo;
extern int olo_loaded;
#endif

struct seg_t;
void P_CalcSegLength(seg_t *lseg);

#endif

//----------------------------------------------------------------------------
//
// $Log: p_setup.h,v $
// Revision 1.3  1998/05/03  23:03:31  killough
// beautification, add external declarations for blockmap
//
// Revision 1.2  1998/01/26  19:27:28  phares
// First rev with no ^Ms
//
// Revision 1.1.1.1  1998/01/19  14:03:08  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------