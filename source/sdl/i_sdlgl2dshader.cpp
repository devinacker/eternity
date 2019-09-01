//
// The Eternity Engine
// Copyright (C) 2019 James Haley, Max Waine, et al.
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
// Purpose: SDL-specific GL 2D-in-3D video code, but this one uses ! S H A D E R S !
// Authors: James Haley, Max Waine
//

#ifdef EE_FEATURE_OPENGL

// SDL headers
#include "SDL.h"
#include "../gl/gl_core_2_1.h"
#include "SDL_opengl.h"

// HAL header
#include "../hal/i_platform.h"

// DOOM headers
#include "../z_zone.h"
#include "../d_main.h"
#include "../i_system.h"
#include "../v_misc.h"
#include "../v_video.h"
#include "../version.h"
#include "../w_wad.h"

// Local driver header
#include "i_sdlgl2dshader.h"

// GL module headers
#include "../gl/gl_primitives.h"
#include "../gl/gl_projection.h"
#include "../gl/gl_texture.h"
#include "../gl/gl_vars.h"

//=============================================================================
//
// WM-related stuff (see i_input.c)
//

void UpdateGrab(SDL_Window *window);
bool MouseShouldBeGrabbed();
void UpdateFocus(SDL_Window *window);

//=============================================================================
//
// Static Data
//

// Temporary screen surface; this is what the game will draw itself into.
static SDL_Surface *screen;

static SDL_GLContext glcontext;

// 32-bit converted palette for translation of the screen to 32-bit pixel data.
static Uint32 RGB8to32[256];
static byte   cachedpal[768];

// GL texture sizes sufficient to hold the screen buffer as a texture
static unsigned int framebuffer_umax;
static unsigned int framebuffer_vmax;

// maximum texture coordinates to put on right- and bottom-side vertices
static GLfloat texcoord_smax;
static GLfloat texcoord_tmax;

// GL texture names
static GLuint textureid;

// Bump amount used to avoid cache misses on power-of-two-sized screens
static int bump;

// Data for vertex binding
static GLfloat screenVertices[4*2];
static GLfloat screenTexCoords[4*2];

static const GLubyte screenVtxOrder[3*2] = { 0, 1, 3, 3, 1, 2 };

//=============================================================================
//
// Shaders
//
const GLchar *shader_source_vertex = R"(
#version 120

layout(location = 0) in vec4 in_position;

void main()
{
   gl_Position = in_position;
}
)";

const GLchar *shader_source_fragment = R"(
#version 120
uniform sampler2D _Indices_in;
uniform sampler2D _Palette;
uniform vec2 tex_size;

//layout(location = 0) out vec4 out_color;

void main()
{
   float paletteIndex = texture2D(_Indices_in, gl_FragCoord.xy / tex_size).r;

   // add half a pixel to the index to fix interpolation issues
   float4 col = texture2D(_Palette, vec2(paletteIndex + (.5/256.0), 0.0) );
   col.a = 1.0;
   gl_fragColor = col;
}
)";

//=============================================================================
//
// Graphics Code
//

//
// SDLGL2DShaderVideoDriver::DrawPixels
//
// Protected method.
//
void SDLGL2DShaderVideoDriver::DrawPixels(void *buffer, unsigned int destwidth)
{
   //Uint32 *fb = static_cast<Uint32 *>(buffer);

   //for(int y = 0; y < screen->h; y++)
   //{
   //   byte   *src  = static_cast<byte *>(screen->pixels) + y * screen->pitch;
   //   Uint32 *dest = fb + y * destwidth;

   //   for(int x = 0; x < screen->w - bump; x++)
   //   {
   //      *dest = RGB8to32[*src];
   //      ++src;
   //      ++dest;
   //   }
   //}
}

//
// SDLGL2DShaderVideoDriver::FinishUpdate
//
void SDLGL2DShaderVideoDriver::FinishUpdate()
{
   // haleyjd 10/08/05: from Chocolate DOOM:
   UpdateGrab(window);

   // Don't update the screen if the window isn't visible.
   // Not doing this breaks under Windows when we alt-tab away
   // while fullscreen.
   if(!(SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN))
      return;

   // TODO: This

   // push the frame
   SDL_GL_SwapWindow(window);
}

//
// SDLGL2DShaderVideoDriver::ReadScreen
//
void SDLGL2DShaderVideoDriver::ReadScreen(byte *scr)
{
   if(bump == 0 && screen->pitch == screen->w)
   {
      // full block blit
      memcpy(scr, static_cast<byte *>(screen->pixels), video.width * video.height);
   }
   else
   {
      // must copy one row at a time
      for(int y = 0; y < screen->h; y++)
      {
         byte *src  = static_cast<byte *>(screen->pixels) + y * screen->pitch;
         byte *dest = scr + y * video.width;

         memcpy(dest, src, screen->w - bump);
      }
   }
}

//
// SDLGL2DShaderVideoDriver::SetPalette
//
void SDLGL2DShaderVideoDriver::SetPalette(byte *pal)
{
   byte *temppal;

   // Cache palette if a new one is being set (otherwise the gamma setting is 
   // being changed)
   if(pal)
      memcpy(cachedpal, pal, 768);

   temppal = cachedpal;

   // Create 32-bit translation lookup
   for(int i = 0; i < 256; i++)
   {
      RGB8to32[i] =
         (static_cast<Uint32>(0xff) << 24) |
         (static_cast<Uint32>(gammatable[usegamma][*(temppal + 0)]) << 16) |
         (static_cast<Uint32>(gammatable[usegamma][*(temppal + 1)]) <<  8) |
         (static_cast<Uint32>(gammatable[usegamma][*(temppal + 2)]) <<  0);

      temppal += 3;
   }
}

//
// SDLGL2DShaderVideoDriver::SetPrimaryBuffer
//
void SDLGL2DShaderVideoDriver::SetPrimaryBuffer()
{
   // Bump up size of power-of-two framebuffers
   if(video.width == 512 || video.width == 1024 || video.width == 2048)
      bump = 4;
   else
      bump = 0;

   // Create screen surface for the high-level code to render the game into
   if(!(screen = SDL_CreateRGBSurfaceWithFormat(0, video.width + bump, video.height,
                                                0, SDL_PIXELFORMAT_INDEX8)))
      I_Error("SDLGL2DVideoDriver::SetPrimaryBuffer: failed to create screen temp buffer\n");

   // Point screens[0] to 8-bit temp buffer
   video.screens[0] = static_cast<byte *>(screen->pixels);
   video.pitch      = screen->pitch;
}

//
// SDLGL2DShaderVideoDriver::UnsetPrimaryBuffer
//
void SDLGL2DShaderVideoDriver::UnsetPrimaryBuffer()
{
   if(screen)
   {
      SDL_FreeSurface(screen);
      screen = nullptr;
   }
   video.screens[0] = nullptr;
}

//
// SDLGL2DShaderVideoDriver::ShutdownGraphics
//
void SDLGL2DShaderVideoDriver::ShutdownGraphics()
{
   ShutdownGraphicsPartway();

   // quit SDL video
   SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

//
// SDLGL2DShaderVideoDriver::ShutdownGraphicsPartway
//
void SDLGL2DShaderVideoDriver::ShutdownGraphicsPartway()
{
   // haleyjd 06/21/06: use UpdateGrab here, not release
   UpdateGrab(window);

   // Code to allow changing resolutions in OpenGL.
   // Must shutdown everything.

   // Delete textures and clear names
   if(textureid)
   {
      glDeleteTextures(1, &textureid);
      textureid = 0;
   }

   // Destroy the "primary buffer" screen surface
   UnsetPrimaryBuffer();

   // Destroy the GL context
   SDL_GL_DeleteContext(glcontext);
   glcontext = nullptr;

   // Destroy the window
   SDL_DestroyWindow(window);
   window = nullptr;
}

// Config-to-GL enumeration lookups

// Configurable texture filtering parameters
static GLint textureFilterParams[CFG_GL_NUMFILTERS] =
{
   GL_LINEAR,
   GL_NEAREST
};

//
// SDLGL2DShaderVideoDriver::InitGraphicsMode
//
bool SDLGL2DShaderVideoDriver::InitGraphicsMode()
{
   bool    wantfullscreen = false;
   bool    wantdesktopfs  = false;
   bool    wantvsync      = false;
   bool    wanthardware   = false; // Not used - this is always "hardware".
   bool    wantframe      = true;
   int     v_w            = 640;
   int     v_h            = 480;
   int     v_displaynum   = 0;
   int     window_flags   = SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI;
   GLint   texfiltertype  = GL_LINEAR;

   // Get video commands and geometry settings

   // Allow end-user GL colordepth setting
   switch(cfg_gl_colordepth)
   {
   case 16: // Valid supported values
   case 24:
   case 32:
      colordepth = cfg_gl_colordepth;
      break;
   default:
      colordepth = 32;
      break;
   }

   // Allow end-user GL texture filtering specification
   if(cfg_gl_filter_type >= 0 && cfg_gl_filter_type < CFG_GL_NUMFILTERS)
      texfiltertype = textureFilterParams[cfg_gl_filter_type];

   // haleyjd 04/11/03: "vsync" or page-flipping support
   if(use_vsync)
      wantvsync = true;

   // set defaults using geom string from configuration file
   I_ParseGeom(i_videomode, &v_w, &v_h, &wantfullscreen, &wantvsync,
               &wanthardware, &wantframe, &wantdesktopfs);

   // haleyjd 06/21/06: allow complete command line overrides but only
   // on initial video mode set (setting from menu doesn't support this)
   I_CheckVideoCmds(&v_w, &v_h, &wantfullscreen, &wantvsync, &wanthardware,
                    &wantframe, &wantdesktopfs);

   if(!wantframe)
      window_flags |= SDL_WINDOW_BORDERLESS;

   // Set GL attributes through SDL
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
   SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
   SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   colordepth >= 24 ? 8 : 5);
   SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, colordepth >= 24 ? 8 : 5);
   SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  colordepth >= 24 ? 8 : 5);
   SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, colordepth == 32 ? 8 : 0);

   if(displaynum < SDL_GetNumVideoDisplays())
      v_displaynum = displaynum;
   else
      displaynum = 0;

   if(!(window = SDL_CreateWindow(ee_wmCaption,
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(v_displaynum),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(v_displaynum),
                                  v_w, v_h, window_flags)))
   {
      I_FatalError(I_ERR_KILL, "Couldn't create OpenGL window %dx%d\n"
                               "SDL Error: %s\n", v_w, v_h, SDL_GetError());
   }

#if EE_CURRENT_PLATFORM == EE_PLATFORM_MACOSX
   // this and the below #else block are done here as monitor video mode isn't
   // set when SDL_WINDOW_FULLSCREEN (sans desktop) is ORed in during window creation
   if(wantfullscreen)
      SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
#else
   if(wantfullscreen && wantdesktopfs)
      SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
   else if(wantfullscreen) // && !wantdesktopfs
      SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
#endif

   if(!(glcontext = SDL_GL_CreateContext(window)))
   {
      I_FatalError(I_ERR_KILL, "Couldn't create OpenGL context\n"
                               "SDL Error: %s\n", SDL_GetError());
   }

   if(ogl_LoadFunctions() == ogl_LOAD_FAILED)
      I_FatalError(I_ERR_KILL, "Couldn't load OpenGL functions\n");

   // Set swap interval through SDL (must be done after context is created)
   SDL_GL_SetSwapInterval(wantvsync ? 1 : 0); // OMG vsync!

   Uint32 format;
   if(colordepth == 32)
      format = SDL_PIXELFORMAT_RGBA32;
   else if(colordepth == 24)
      format = SDL_PIXELFORMAT_RGB24;
   else // 16
      format = SDL_PIXELFORMAT_RGB555;

   if(!(screen = SDL_CreateRGBSurfaceWithFormat(0, v_w, v_h, 0, format)))
   {
      I_FatalError(I_ERR_KILL, "Couldn't set RGB surface with colordepth %d, format %s\n",
                   colordepth, SDL_GetPixelFormatName(format));
   }

   // Set viewport
   // This is necessary for high-DPI displays (tested so far on macOS).
   int drawableW = 0;
   int drawableH = 0;
   SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
   if(!drawableW || !drawableH)
   {
      // If the function somehow fails, reset to v_w and v_h
      drawableW = v_w;
      drawableH = v_h;
   }
   glViewport(0, 0, static_cast<GLsizei>(drawableW), static_cast<GLsizei>(drawableH));

   // Calculate framebuffer texture sizes
   framebuffer_umax = GL_MakeTextureDimension(static_cast<unsigned int>(v_w));
   framebuffer_vmax = GL_MakeTextureDimension(static_cast<unsigned int>(v_h));

   // calculate right- and bottom-side texture coordinates
   texcoord_smax = static_cast<GLfloat>(v_w) / framebuffer_umax;
   texcoord_tmax = static_cast<GLfloat>(v_h) / framebuffer_vmax;

   UpdateFocus(window);
   UpdateGrab(window);

   // Init Cardboard video metrics
   video.width     = v_w;
   video.height    = v_h;
   video.bitdepth  = 8;
   video.pixelsize = 1;

   UnsetPrimaryBuffer();
   SetPrimaryBuffer();

   // Set initial palette
   SetPalette(static_cast<byte *>(wGlobalDir.cacheLumpName("PLAYPAL", PU_CACHE)));

   return false;
}

// The one and only global instance of the SDL GL 2D-in-3D video driver.
SDLGL2DShaderVideoDriver i_sdlgl2dshadervideodriver;

#endif

// EOF

