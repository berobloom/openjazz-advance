
/**
 *
 * @file gba.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _GBA_H
#define _GBA_H

#include "platform_interface.h"

#ifdef __GBA__

/* Device uses a minimal SDL 1.2 stand-in, see gba/sdl/SDL.h. Game data comes
   from the baked pack in the cartridge ROM, see io/pack.cpp. */

// Video config
// Drawn at 320x200 and scaled to the 240x160 screen (see gba/sdl_gba.cpp)
#define DEFAULT_SCREEN_WIDTH 320
#define DEFAULT_SCREEN_HEIGHT 200

/* The 320x200 video surface keeps only the pixels that reach the screen:
   columns 0, 1 and 3 of every four and rows 0, 1, 3 and 4 of every five. Code
   that writes canvas pixels directly maps them with these (see io/gfx/video.h). */
#define OJ_SCALED_CANVAS
/* Division by 3 and 5 as a multiplication, which is exact over these ranges.
   Thumb code has no long multiplication, so for n / 3 it calls the slow
   division routine. */
inline int divideBy3 (int n) { return (static_cast<unsigned int>(n) < 98304)? (static_cast<unsigned int>(n) * 43691u) >> 17: n / 3; }
inline int divideBy5 (int n) { return (static_cast<unsigned int>(n) < 81920)? (static_cast<unsigned int>(n) * 52429u) >> 18: n / 5; }
/// Canvas column of the given stored column
inline int scaledColumnOf (int column) { int q = divideBy3(column), j = column - (q * 3); return (q << 2) + j + (j >> 1); }
/// Canvas row of the given stored row
inline int scaledRowOf (int row) { return ((row * 5) + 2) >> 2; }
/// Number of stored columns left of the given canvas column
inline int scaledColumns (int x) { int r = x & 3; return (3 * (x >> 2)) + r - (r == 3); }
/// Number of stored rows above the given canvas row
inline int scaledRows (int y) { int q = divideBy5(y), r = y - (q * 5); return (q << 2) + r - (r >= 3); }
#define FULLSCREEN_ONLY
#define NO_RESIZE
#define T_MENU_FRAME 0 // Flips wait for VBlank already

// Levels use the tile and sprite hardware at the native resolution (see gba/level_gba.h)
#define OJ_TILED_LEVEL

// Hot loops run from IWRAM as ARM code
#define OJ_FAST_CODE __attribute__((section(".iwram.oj"), target("arm"), noinline))

// Logging goes to the emulator's debug output only (see gba.cpp)
#define NO_LOGFILE

// Keyboard config
#define NO_KEYBOARD_CFG

/* The buttons arrive as key presses (see gba/sdl_gba.cpp):
   D-pad: arrow keys, A: space, B: left alt, L: P, R: right ctrl,
   Start: escape, Select: F9 */
#define DEFAULT_KEY_ENTER (SDLK_SPACE)

/// Stop with a message if the stack has run into the IWRAM data below it
void gbaCheckStack();

/// Bytes of IWRAM stack never used so far
int gbaFreeStack();

class GbaPlatform final : public IPlatform {
	public:
		GbaPlatform();

		void ErrorNoDatafiles() override;
};

#endif

#endif
