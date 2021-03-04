//
// The Eternity Engine
// Copyright(C) 2020 James Haley, Max Waine, et al.
// Copyright(C) 2020 Ethan Watson
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
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//----------------------------------------------------------------------------
//
// Purpose: Renderer context
// Some code is derived from Rum & Raisin Doom, by Ethan Watson, used under
// terms of the GPLv3.
//
// Authors: Max Waine
//

#include <atomic>
#include <thread>

#include "c_io.h"
#include "c_runcmd.h"
#include "doomstat.h"
#include "m_misc.h"
#include "hal/i_timer.h"
#include "i_video.h"
#include "r_context.h"
#include "r_main.h"
#include "r_state.h"
#include "v_misc.h"

struct renderdata_t
{
   rendercontext_t  context;
   std::thread      thread;
   std::atomic_bool running;
   std::atomic_bool shouldquit;
   std::atomic_bool framewaiting;
   std::atomic_bool framefinished;
};

static renderdata_t *renderdatas      = nullptr;
static int           prev_numcontexts = 0;

static bool          temp_dgafaboutyourcpu = true; // THREAD_FIXME: DELETE THIS

//
// Grabs a given render context
//
rendercontext_t &R_GetContext(int index)
{
   return renderdatas[index].context;
}

//
// Frees up the dynamically allocated members of a context that aren't tagged PU_VALLOC
// Also tidies up threads of that context's data
//
void R_freeContext(rendercontext_t &context)
{
   // THREAD_FIXME: Verify this catches everything
   if(context.spritecontext.drawsegs_xrange)
      efree(context.spritecontext.drawsegs_xrange);
   if(context.spritecontext.vissprites)
      efree(context.spritecontext.vissprites);
   if(context.spritecontext.vissprite_ptrs)
      efree(context.spritecontext.vissprite_ptrs);
   if(context.spritecontext.sectorvisited)
      efree(context.spritecontext.sectorvisited);
}

//
// Frees up actual renderdatas, which need waiting on before we can safely free
//
void R_freeData(renderdata_t &data)
{
   data.shouldquit.store(true);

   while(data.running.load())
      i_haltimer.Sleep(1);

   R_freeContext(data.context);
}

void R_FreeContexts()
{
   R_freeContext(r_globalcontext);

   if(renderdatas)
   {
      for(int currentcontext = 0; currentcontext < prev_numcontexts; currentcontext++)
         R_freeData(renderdatas[currentcontext]);
      efree(renderdatas);
   }
}

//
// This function is always going on in the background
// so that threads don't need to constantly be spawned
//
static void R_contextThreadFunc(renderdata_t *data)
{
   data->running.exchange(true);

   while(!data->shouldquit.load())
   {
      if(data->framewaiting.exchange(false))
      {
         R_RenderViewContext(data->context);
         data->framefinished.store(true);
         // THREAD_TODO: Wait here an appropriate amount of time once FPS limiting is in
      }

      if(temp_dgafaboutyourcpu)
         std::this_thread::yield(); // I YIELD MY TIME, FUCK YOU!
      else
         i_haltimer.Sleep(1);
   }

   data->running.exchange(false);
}

//
// Initialises all the render contexts
//
void R_InitContexts(const int width)
{
   prev_numcontexts = r_numcontexts;

   r_globalcontext = {};
   r_globalcontext.bufferindex  = -1;
   r_globalcontext.bounds.startcolumn  = 0;
   r_globalcontext.bounds.endcolumn    = width;
   r_globalcontext.bounds.fstartcolumn = 0.0f;
   r_globalcontext.bounds.fendcolumn   = float(width);
   r_globalcontext.bounds.numcolumns   = width;

   if(r_numcontexts == 1)
   {
      r_globalcontext.portalcontext.portalrender = { false, MAX_SCREENWIDTH, 0 };

      if(numsectors && gamestate == GS_LEVEL)
         r_globalcontext.spritecontext.sectorvisited = ecalloctag(bool *, numsectors, sizeof(bool), PU_LEVEL, nullptr);

      return;
   }

   renderdatas = estructalloc(renderdata_t, r_numcontexts);

   const float contextwidth = float(width) / float(r_numcontexts);

   for(int currentcontext = 0; currentcontext < r_numcontexts; currentcontext++)
   {
      rendercontext_t &context = renderdatas[currentcontext].context;

      context.bufferindex = currentcontext;

      context.bounds.fstartcolumn = float(currentcontext)     * contextwidth;
      context.bounds.fendcolumn   = float(currentcontext + 1) * contextwidth;
      context.bounds.startcolumn  = int(roundf(context.bounds.fstartcolumn));
      context.bounds.endcolumn    = int(roundf(context.bounds.fendcolumn));
      context.bounds.numcolumns   = context.bounds.endcolumn - context.bounds.startcolumn;

      context.portalcontext.portalrender = { false, MAX_SCREENWIDTH, 0 };

      if(numsectors && gamestate == GS_LEVEL)
         context.spritecontext.sectorvisited = ecalloctag(bool *, numsectors, sizeof(bool), PU_LEVEL, nullptr);

      // Wait until this context's thread is done running before creating a new one
      while(renderdatas[currentcontext].running.load())
         i_haltimer.Sleep(1);
      renderdatas[currentcontext].thread = std::thread(&R_contextThreadFunc, &renderdatas[currentcontext]);
   }
}

void R_RefreshContexts()
{
   if(r_numcontexts == 1)
   {
      r_globalcontext.spritecontext.sectorvisited = ecalloctag(bool *, numsectors, sizeof(bool), PU_LEVEL, nullptr);
      return;
   }

   for(int currentcontext = 0; currentcontext < r_numcontexts; currentcontext++)
   {
      rendercontext_t &context = renderdatas[currentcontext].context;

      context.spritecontext.sectorvisited = ecalloctag(bool *, numsectors, sizeof(bool), PU_LEVEL, nullptr);
   }
}

//
// Runs all the contexts by setting the waiting-for-frame atomics bool to true,
// then waits for the frame-finished-rendering atomic bools to be true
// (setting them to false after)
//
void R_RunContexts()
{
   int finishedcontexts = 0;

   for(int currentcontext = 0; currentcontext < r_numcontexts; currentcontext++)
      renderdatas[currentcontext].framewaiting.store(true);

   while(finishedcontexts != r_numcontexts)
   {
      for(int currentcontext = 0; currentcontext < r_numcontexts; currentcontext++)
      {
         if(renderdatas[currentcontext].framefinished.exchange(false))
            finishedcontexts++;
      }
   }
}

#if 0
VARIABLE_BOOLEAN(temp_dgafaboutyourcpu, nullptr, yesno);
CONSOLE_VARIABLE(r_gofast, temp_dgafaboutyourcpu, cf_buffered) {}

VARIABLE_INT(r_numcontexts, nullptr, 0, UL, nullptr);
CONSOLE_VARIABLE(r_numcontexts, r_numcontexts, cf_buffered)
{
   const int maxcontexts = std::thread::hardware_concurrency();

   if(r_numcontexts == 0)
      r_numcontexts = maxcontexts - 1; // allow scrolling left from 1 to maxcontexts
   else if(r_numcontexts == maxcontexts + 1)
      r_numcontexts = 1; // allow scrolling right from maxcontexts to 1
   else if(r_numcontexts > maxcontexts)
   {
      C_Printf(FC_ERROR "Warning: r_numcontexts's current maximum is %d, resetting to 1", maxcontexts);
      r_numcontexts = 1;
   }

   I_SetMode();
}
#endif

// EOF

