
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
#define FULLSCREEN_ONLY
#define NO_RESIZE
#define T_MENU_FRAME 0 // Flips wait for VBlank already

// Cutscenes do not fit in RAM yet
#define NO_STARTUP_SCENE

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

class GbaPlatform final : public IPlatform {
	public:
		GbaPlatform();

		void ErrorNoDatafiles() override;
};

#endif

#endif
