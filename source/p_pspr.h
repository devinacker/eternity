// Emacs style mode select   -*- C -*- 
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
//  Sprite animation.
//
//-----------------------------------------------------------------------------

#ifndef __P_PSPR__
#define __P_PSPR__

// Basic data types.
// Needs fixed point, and BAM angles.

#include "m_fixed.h"
#include "tables.h"
#include "doomdef.h"
#include "p_mobj.h"

// Needs to include the precompiled sprite animation tables.
//
// Header generated by multigen utility.
// This includes all the data for thing animation,
// i.e. the Thing Atrributes table and the Frame Sequence table.

#include "info.h"

//
// Frame flags:
// handles maximum brightness (torches, muzzle flare, light sources)
//

#define FF_FULLBRIGHT   0x8000  /* flag in thing->frame */
#define FF_FRAMEMASK    0x7fff

// haleyjd 09/16/07: silencer weapon volume reduction
#define WEAPON_VOLUME_SILENCED 50

//
// Overlay psprites are scaled shapes
// drawn directly on the view screen,
// coordinates are given for a 320*200 view screen.
//

typedef enum
{
  ps_weapon,
  ps_flash,
  NUMPSPRITES
} psprnum_t;

typedef struct pspdef_s
{
  state_t *state;       // a NULL state means not active
  int     tics;
  fixed_t sx;
  fixed_t sy;
  int trans;
} pspdef_t;

extern int weapon_preferences[2][NUMWEAPONS+1];      // killough 5/2/98
int P_WeaponPreferred(int w1, int w2);
extern int weapon_speed, default_weapon_speed;
extern int action_from_pspr;                     // haleyjd 05/21/08

void P_SetPsprite(player_t *player, int position, statenum_t stnum);

int P_NextWeapon(player_t *player);
int P_PrevWeapon(player_t *player);

struct player_s;
weapontype_t P_SwitchWeapon(struct player_s *player);
boolean P_CheckAmmo(struct player_s *player);
void P_SetupPsprites(struct player_s *curplayer);
void P_MovePsprites(struct player_s *curplayer);
void P_DropWeapon(struct player_s *player);

extern void P_BulletSlope(mobj_t *mo);

weaponinfo_t *P_GetReadyWeapon(player_t *player);
weaponinfo_t *P_GetPlayerWeapon(player_t *player, int index);

#endif

//----------------------------------------------------------------------------
//
// $Log: p_pspr.h,v $
// Revision 1.5  1998/05/03  22:54:44  killough
// beautification, add external decls formerly in p_local.h
//
// Revision 1.4  1998/02/15  02:48:15  phares
// User-defined keys
//
// Revision 1.3  1998/02/09  03:06:18  killough
// Add player weapon preference options
//
// Revision 1.2  1998/01/26  19:27:25  phares
// First rev with no ^Ms
//
// Revision 1.1.1.1  1998/01/19  14:03:09  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------
