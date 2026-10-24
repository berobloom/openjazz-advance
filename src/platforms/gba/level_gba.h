
/**
 *
 * @file level_gba.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Levels drawn with the GBA's tile and sprite hardware (OJ_TILED_LEVEL).
 *
 * While a level plays, the screen shows the level at its native 240x160:
 * - BG2 holds the background tiles and BG1 the foreground tiles. Both are
 *   streamed 8x8 pixels at a time into a cache in VRAM.
 * - Sprites become hardware objects between the two.
 * - The sky is the backdrop colour, changed for each scanline by HBlank DMA.
 * - BG0 holds the panel, scaled down from 320x33, and an overlay for text and
 *   other drawing on the canvas.
 *
 * The engine keeps drawing through SDL calls. Blits onto spriteLayer() become
 * hardware objects, and blits onto the canvas go to the overlay.
 *
 */


#ifndef OJ_GBA_LEVEL_H
#define OJ_GBA_LEVEL_H

struct SDL_Surface;

namespace gbaLevel {

	/// Screen rows taken by the panel at the bottom
	const int HUD_HEIGHT = 26;

	/**
	 * Switch to drawing levels with the tile hardware. The canvas becomes the
	 * overlay, at the native 240x160.
	 *
	 * @param tileSet Level tiles, 32 pixels wide, one below the other
	 * @param tiles Number of tiles
	 * @param key Transparent tile pixel value
	 * @param black Pixel value of the black drawn behind some tiles
	 */
	void enter (SDL_Surface* tileSet, int tiles, int key, int black);

	/// Return to the bitmap screen, with the 320x200 canvas
	void leave ();

	/// Whether levels are being drawn with the tile hardware
	bool active ();

	/**
	 * Start drawing a frame.
	 *
	 * @param viewX Level pixel at the screen's left edge
	 * @param viewY Level pixel at the screen's top edge
	 */
	void beginFrame (int viewX, int viewY);

	/**
	 * Set the tiles shown in one level cell.
	 *
	 * @param x Cell column
	 * @param y Cell row
	 * @param background Background tile, or -1 for none
	 * @param black Whether black shows behind the background tile
	 * @param foreground Foreground tile, or -1 for none
	 */
	void setCell (int x, int y, int background, bool black, int foreground);

	/**
	 * Set the colours behind all tiles.
	 *
	 * @param first Palette index for the top screen row
	 * @param scale Screen rows per palette index, or 0 for one colour throughout
	 */
	void setSky (int first, int scale);

	/// Surface whose blits become hardware sprites
	SDL_Surface* spriteLayer ();

	/// Priority of the sprites blitted from now on: 2 in front of the background, 3 behind it
	void setSpritePriority (int priority);

	/// Surface for text and other drawing in front of the level
	SDL_Surface* overlay ();

	/// Something the panel shows, and the columns of the 320x33 panel it takes
	struct HudField {
		int value;
		short left, right;
	};

	/**
	 * Check whether the panel has to be redrawn, and if so, make the canvas the
	 * panel's 320x200 surface, limited to the columns of the fields that changed.
	 *
	 * @param fields Everything the panel shows
	 * @param count Number of fields, at most 16
	 *
	 * @return Whether to draw the panel, then call endHud()
	 */
	bool beginHud (const HudField* fields, int count);

	/// Show the panel just drawn, and make the overlay the canvas again
	void endHud ();

	/// Draws with the tile hardware while it exists
	class Scope {
		public:
			Scope (SDL_Surface* tileSet, int tiles, int key, int black) { enter(tileSet, tiles, key, black); }
			~Scope () { leave(); }
	};


	/* For the SDL stand-in (see sdl_gba.cpp) */

	/// Queue a sprite for the frame being drawn; remapHash identifies remap's contents
	void sprite (const unsigned char* pixels, int pitch, int w, int h, int x, int y,
		int key, const unsigned char* remap, unsigned int remapHash);

	/// Draw on the overlay; the area is already clipped to it
	void overlayBlit (const unsigned char* pixels, int pitch, int x, int y, int w, int h,
		int key, const unsigned char* remap);

	/// Fill part of the overlay; the area is already clipped to it
	void overlayFill (int x, int y, int w, int h, int color);

	/// Prepare the frame just drawn for display, with the given palette (BGR555)
	void commit (const unsigned short* palette);

	/// At VBlank: show the committed frame if there is one, and restart the sky
	void vblank (bool committed);

}

/* Implemented in sdl_gba.cpp */

/// Wait until the last flip is on show
void gbaWaitFlip ();

/// Free the bitmap screen's pixels
void gbaReleaseScreen ();

/// Allocate the bitmap screen's pixels again
bool gbaRestoreScreen ();

#endif
