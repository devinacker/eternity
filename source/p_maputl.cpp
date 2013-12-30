// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2013 James Haley et al.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
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
//      Movement/collision utility functions,
//      as used by function in p_map.c.
//      BLOCKMAP Iterator functions,
//      and some PIT_* functions to use for iteration.
//
//-----------------------------------------------------------------------------

#include "z_zone.h"

#include "doomstat.h"
#include "m_bbox.h"
#include "p_clipen.h"
#include "p_map3d.h"
#include "p_mapcontext.h"
#include "p_maputl.h"
#include "p_setup.h"
#include "polyobj.h"
#include "r_data.h"
#include "r_main.h"
#include "r_portal.h"
#include "r_state.h"


//
// P_AproxDistance
// Gives an estimation of distance (not exact)
//
fixed_t P_AproxDistance(fixed_t dx, fixed_t dy)
{
   dx = D_abs(dx);
   dy = D_abs(dy);
   if(dx < dy)
      return dx+dy-(dx>>1);
   return dx+dy-(dy>>1);
}

//
// P_PointOnLineSide
// Returns 0 or 1
//
// killough 5/3/98: reformatted, cleaned up
//
int P_PointOnLineSide(fixed_t x, fixed_t y, line_t *line)
{
   return
      !line->dx ? x <= line->v1->x ? line->dy > 0 : line->dy < 0 :
      !line->dy ? y <= line->v1->y ? line->dx < 0 : line->dx > 0 :
      FixedMul(y-line->v1->y, line->dx>>FRACBITS) >=
      FixedMul(line->dy>>FRACBITS, x-line->v1->x);
}

//
// P_BoxOnLineSide
// Considers the line to be infinite
// Returns side 0 or 1, -1 if box crosses the line.
//
// killough 5/3/98: reformatted, cleaned up
//
int P_BoxOnLineSide(fixed_t *tmbox, line_t *ld)
{
   int p;

   switch (ld->slopetype)
   {
   default: // shut up compiler warnings -- killough
   case ST_HORIZONTAL:
      return
      (tmbox[BOXBOTTOM] > ld->v1->y) == (p = tmbox[BOXTOP] > ld->v1->y) ?
        p ^ (ld->dx < 0) : -1;
   case ST_VERTICAL:
      return
        (tmbox[BOXLEFT] < ld->v1->x) == (p = tmbox[BOXRIGHT] < ld->v1->x) ?
        p ^ (ld->dy < 0) : -1;
   case ST_POSITIVE:
      return
        P_PointOnLineSide(tmbox[BOXRIGHT], tmbox[BOXBOTTOM], ld) ==
        (p = P_PointOnLineSide(tmbox[BOXLEFT], tmbox[BOXTOP], ld)) ? p : -1;
   case ST_NEGATIVE:
      return
        (P_PointOnLineSide(tmbox[BOXLEFT], tmbox[BOXBOTTOM], ld)) ==
        (p = P_PointOnLineSide(tmbox[BOXRIGHT], tmbox[BOXTOP], ld)) ? p : -1;
    }
}

//
// P_PointOnDivlineSide
// Returns 0 or 1.
//
// killough 5/3/98: reformatted, cleaned up
//
int P_PointOnDivlineSide(fixed_t x, fixed_t y, divline_t *line)
{
   return
      !line->dx ? x <= line->x ? line->dy > 0 : line->dy < 0 :
      !line->dy ? y <= line->y ? line->dx < 0 : line->dx > 0 :
      (line->dy^line->dx^(x -= line->x)^(y -= line->y)) < 0 ? (line->dy^x) < 0 :
      FixedMul(y>>8, line->dx>>8) >= FixedMul(line->dy>>8, x>>8);
}

//
// P_MakeDivline
//
void P_MakeDivline(line_t *li, divline_t *dl)
{
   dl->x  = li->v1->x;
   dl->y  = li->v1->y;
   dl->dx = li->dx;
   dl->dy = li->dy;
}

//
// P_InterceptVector
// Returns the fractional intercept point
// along the first divline.
// This is only called by the addthings
// and addlines traversers.
//
// killough 5/3/98: reformatted, cleaned up
//
fixed_t P_InterceptVector(divline_t *v2, divline_t *v1)
{
   fixed_t den = FixedMul(v1->dy>>8, v2->dx) - FixedMul(v1->dx>>8, v2->dy);
   return den ? FixedDiv((FixedMul((v1->x-v2->x)>>8, v1->dy) +
                          FixedMul((v2->y-v1->y)>>8, v1->dx)), den) : 0;
}





//
// THING POSITION SETTING
//

//
// P_LogThingPosition
//
// haleyjd 04/15/2010: thing position logging for debugging demo problems.
// Pass a NULL mobj to close the log.
//
#ifdef THING_LOGGING
void P_LogThingPosition(Mobj *mo, const char *caller)
{
   static FILE *thinglog;

   if(!thinglog)
      thinglog = fopen("thinglog.txt", "w");

   if(!mo)
   {
      if(thinglog)
         fclose(thinglog);
      thinglog = NULL;
      return;
   }

   if(thinglog)
   {
      fprintf(thinglog,
         "%010d:%s:%p:%20s:%+010d:%+010d:%+010d:%+010d\n",
         gametic, caller, mo, mo->info->name, mo->x, mo->y, mo->z, mo->flags);
   }
}
#else
#define P_LogThingPosition(a, b)
#endif

// killough 3/15/98:
//
// A fast function for testing intersections between things and linedefs.
//
// haleyjd: note -- this is never called, and is, according to
// SoM, VERY inaccurate. I don't really know what its for or why
// its here, but I'm leaving it be.
//
bool ThingIsOnLine(Mobj *t, line_t *l)
{
   int dx = l->dx >> FRACBITS;                           // Linedef vector
   int dy = l->dy >> FRACBITS;
   int a = (l->v1->x >> FRACBITS) - (t->x >> FRACBITS);  // Thing-->v1 vector
   int b = (l->v1->y >> FRACBITS) - (t->y >> FRACBITS);
   int r = t->radius >> FRACBITS;                        // Thing radius

   // First make sure bounding boxes of linedef and thing intersect.
   // Leads to quick rejection using only shifts and adds/subs/compares.
   
   if(D_abs(a*2+dx)-D_abs(dx) > r*2 || D_abs(b*2+dy)-D_abs(dy) > r*2)
      return 0;

   // Next, make sure that at least one thing crosshair intersects linedef's
   // extension. Requires only 3-4 multiplications, the rest adds/subs/
   // shifts/xors (writing the steps out this way leads to better codegen).

   a *= dy;
   b *= dx;
   a -= b;
   b = dx + dy;
   b *= r;
   if(((a-b)^(a+b)) < 0)
      return 1;
   dy -= dx;
   dy *= r;
   b = a+dy;
   a -= dy;
   return (a^b) < 0;
}

//
// BLOCK MAP ITERATORS
// For each line/thing in the given mapblock,
// call the passed PIT_* function.
// If the function returns false,
// exit with false without checking anything else.
//

//
// P_BlockLinesIterator
// The validcount flags are used to avoid checking lines
// that are marked in multiple mapblocks,
// so increment validcount before the first call
// to P_BlockLinesIterator, then make one or more calls
// to it.
//
// killough 5/3/98: reformatted, cleaned up
//
bool P_BlockLinesIterator(int x, int y, bool func(line_t*, MapContext *), MapContext *c)
{
   int        offset;
   const int  *list;     // killough 3/1/98: for removal of blockmap limit
   DLListItem<polymaplink_t> *plink; // haleyjd 02/22/06
   
   if(x < 0 || y < 0 || x >= bmapwidth || y >= bmapheight)
      return true;
   offset = y * bmapwidth + x;

   // haleyjd 02/22/06: consider polyobject lines
   plink = polyblocklinks[offset];

   while(plink)
   {
      polyobj_t *po = (*plink)->po;

      if(po->validcount != validcount) // if polyobj hasn't been checked
      {
         int i;
         po->validcount = validcount;
         
         for(i = 0; i < po->numLines; ++i)
         {
            if(po->lines[i]->validcount == validcount) // line has been checked
               continue;
            po->lines[i]->validcount = validcount;
            if(!func(po->lines[i], c))
               return false;
         }
      }
      plink = plink->dllNext;
   }

   // original was reading delimiting 0 as linedef 0 -- phares
   offset = *(blockmap + offset);
   list = blockmaplump + offset;

   // killough 1/31/98: for compatibility we need to use the old method.
   // Most demos go out of sync, and maybe other problems happen, if we
   // don't consider linedef 0. For safety this should be qualified.

   // killough 2/22/98: demo_compatibility check
   // skip 0 starting delimiter -- phares
   if(!demo_compatibility)
      list++;     
   for( ; *list != -1; list++)
   {
      line_t *ld;
      
      // haleyjd 04/06/10: to avoid some crashes during demo playback due to
      // invalid blockmap lumps
      if(*list >= numlines)
         continue;

      ld = &lines[*list];
      if(ld->validcount == validcount)
         continue;       // line has already been checked
      ld->validcount = validcount;
      if(!func(ld, c))
         return false;
   }
   return true;  // everything was checked
}

//
// P_BlockThingsIterator
//
// killough 5/3/98: reformatted, cleaned up
//
bool P_BlockThingsIterator(int x, int y, bool func(Mobj*, MapContext *), MapContext *c)
{
   if(!(x < 0 || y < 0 || x >= bmapwidth || y >= bmapheight))
   {
      mobjblocklink_t *link = blocklinks[y * bmapwidth + x];

      for(; link; link = link->bnext)
         if(!func(link->mo, c))
            return false;
   }
   return true;
}

//
// P_PointToAngle
//
// To get a global angle from cartesian coordinates,
//  the coordinates are flipped until they are in
//  the first octant of the coordinate system, then
//  the y (<=x) is scaled and divided by x to get a
//  tangent (slope) value which is looked up in the
//  tantoangle[] table. The +1 size of tantoangle[]
//  is to handle the case when x==y without additional
//  checking.
//
// killough 5/2/98: reformatted, cleaned up
// haleyjd 01/28/10: restored to Vanilla and made some modifications;
//                   added P_ version for use by gamecode.
//
angle_t P_PointToAngle(fixed_t xo, fixed_t yo, fixed_t x, fixed_t y)
{	
   x -= xo;
   y -= yo;

   if((x | y) == 0)
      return 0;

   if(x >= 0)
   {
      if (y >= 0)
      {
         if(x > y)
         {
            // octant 0
            return tantoangle[SlopeDiv(y, x)];
         }
         else
         {
            // octant 1
            return ANG90 - 1 - tantoangle[SlopeDiv(x, y)];
         }
      }
      else
      {
         y = -y;

         if(x > y)
         {
            // octant 8
            return 0 - tantoangle[SlopeDiv(y, x)];
         }
         else
         {
            // octant 7
            return ANG270 + tantoangle[SlopeDiv(x, y)];
         }
      }
   }
   else
   {
      x = -x;

      if(y >= 0)
      {
         if(x > y)
         {
            // octant 3
            return ANG180 - 1 - tantoangle[SlopeDiv(y, x)];
         }
         else
         {
            // octant 2
            return ANG90 + tantoangle[SlopeDiv(x, y)];
         }
      }
      else
      {
         y = -y;

         if(x > y)
         {
            // octant 4
            return ANG180 + tantoangle[SlopeDiv(y, x)];
         }
         else
         {
            // octant 5
            return ANG270 - 1 - tantoangle[SlopeDiv(x, y)];
         }
      }
   }

   return 0;
}


static mobjblocklink_t    *freeBlockLinkHead;

void P_InitMobjBlockLinks()
{
   freeBlockLinkHead = NULL;
}


mobjblocklink_t  *P_AddMobjBlockLink(Mobj *mo, int bx, int by, int adjacencymask)
{
   mobjblocklink_t *link;
   int index = by * bmapwidth + bx;
   
   if(freeBlockLinkHead)
   {
      link = freeBlockLinkHead;
      freeBlockLinkHead = link->mnext;
   }
   else
      link = (mobjblocklink_t *)Z_Malloc(sizeof(mobjblocklink_t), PU_LEVEL, NULL);
   
   link->mo = mo;
   link->adjacencymask = adjacencymask;
   link->nodeindex = index;
   
   // Add to mobj
   link->mnext = mo->blocklinks;
   mo->blocklinks = link;
   
   // Add to block
   mobjblocklink_t *head = blocklinks[index];
   link->bnext = head;
   link->bprev = NULL;
   
   if(head)
      head->bprev = link;
   
   blocklinks[index] = link;
    
   return link;
}



void P_RemoveMobjBlockLinks(Mobj *mo)
{
   // remove from blocks
   mobjblocklink_t *link;
   mobjblocklink_t *next = mo->blocklinks;
   do
   {
      link = next;
      
      if(!link->bprev)
         blocklinks[link->nodeindex] = link->bnext;
      else
         link->bprev->bnext = link->bnext;
      
      if(link->bnext)
         link->bnext->bprev = link->bprev;
      
      next = link->mnext;
   } while(next);
   
   link->mo = NULL;
   link->mnext = freeBlockLinkHead;
   freeBlockLinkHead = mo->blocklinks;
   mo->blocklinks = NULL;
}


//----------------------------------------------------------------------------
//
// $Log: p_maputl.c,v $
// Revision 1.13  1998/05/03  22:16:48  killough
// beautification
//
// Revision 1.12  1998/03/20  00:30:03  phares
// Changed friction to linedef control
//
// Revision 1.11  1998/03/19  14:37:12  killough
// Fix ThingIsOnLine()
//
// Revision 1.10  1998/03/19  00:40:52  killough
// Change ThingIsOnLine() comments
//
// Revision 1.9  1998/03/16  12:34:45  killough
// Add ThingIsOnLine() function
//
// Revision 1.8  1998/03/09  18:27:10  phares
// Fixed bug in neighboring variable friction sectors
//
// Revision 1.7  1998/03/09  07:19:26  killough
// Remove use of FP for point/line queries
//
// Revision 1.6  1998/03/02  12:03:43  killough
// Change blockmap offsets to 32-bit
//
// Revision 1.5  1998/02/23  04:45:24  killough
// Relax blockmap iterator to demo_compatibility
//
// Revision 1.4  1998/02/02  13:41:38  killough
// Fix demo sync programs caused by last change
//
// Revision 1.3  1998/01/30  23:13:10  phares
// Fixed delimiting 0 bug in P_BlockLinesIterator
//
// Revision 1.2  1998/01/26  19:24:11  phares
// First rev with no ^Ms
//
// Revision 1.1.1.1  1998/01/19  14:03:00  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------

