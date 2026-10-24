
/**
 *
 * @file level_gba.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Levels drawn with the GBA's tile and sprite hardware (see level_gba.h).
 *
 * VRAM holds 928 8x8 tiles for the backgrounds, with the three maps above
 * them. Tiles are never changed while on screen: a tile slot is only reused
 * once no map shown at the last VBlank refers to it, and the panel and overlay
 * get fresh slots when they change. The maps, sprite attributes, scroll
 * offsets and palette are copied in at VBlank, from shadows drawn during the
 * frame.
 *
 */


#include "level_gba.h"

#include "io/gfx/video.h"
#include "io/log.h"
#include "platforms/gba.h"

#include <SDL.h>

#include <gba_dma.h>
#include <gba_systemcalls.h>
#include <gba_video.h>

#include <stdlib.h>
#include <string.h>

namespace {

	const int SCREEN_WIDTH_NATIVE = 240;
	const int SCREEN_HEIGHT_NATIVE = 160;
	const int SLOTS = 928; ///< Background tile slots below the maps
	const int BLANK = 0; ///< Slot of the transparent tile
	const int BLACK = 1; ///< Slot of the black tile
	const unsigned short NONE = 0xFFFF; ///< No slot
	const int MAP_HUD = 29; ///< Screen block of BG0, the panel and overlay
	const int MAP_FG = 30; ///< Screen block of BG1, the foreground
	const int MAP_BG = 31; ///< Screen block of BG2, the background
	const int HUD_TOP = 16; ///< First map row of the panel
	const int HUD_SOURCE_TOP = 167; ///< First canvas row of the 320x200 panel
	const int HUD_STORED_TOP = 134; ///< First stored row of the panel: scaledRows(HUD_SOURCE_TOP)
	const int OBJ_UNITS = 512; ///< 8bpp sprite tiles in sprite VRAM
	const int OBJ_ENTRIES = 48; ///< Sprite images kept in sprite VRAM
	const int OBJ_PIECES = 12; ///< Most hardware sprites per image

	u32* const bgTiles = reinterpret_cast<u32*>(VRAM);
	u32* const objTiles = reinterpret_cast<u32*>(VRAM + 0x10000);
	void* const oamMemory = reinterpret_cast<void*>(0x07000000);
	const unsigned short OBJ_HIDDEN = 1 << 9; ///< Sprite attribute 0: not shown
	const unsigned short OBJ_256_COLORS = 1 << 13; ///< Sprite attribute 0: 8 bits per pixel

	/// Hardware sprite widths and heights by shape and size
	const unsigned char objWidths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
	const unsigned char objHeights[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

	struct Piece {
		short x, y; ///< Offset in the image
		unsigned char shape, size; ///< Hardware sprite shape and size
		unsigned short tile; ///< First 8bpp tile, from the image's first
	};

	struct ObjEntry {
		const unsigned char* pixels; ///< First pixel, or nullptr if free
		unsigned int remapHash;
		unsigned int lastUsed; ///< Frame that last drew it
		unsigned short w, h, pitch;
		short key;
		unsigned short tile; ///< First 8bpp tile in sprite VRAM
		unsigned short tiles;
		unsigned char pieces;
		Piece piece[OBJ_PIECES];
	};

	struct State {
		SDL_Surface* tileSet;
		int tiles;
		int key; ///< Transparent tile pixel value
		int black; ///< Black pixel value
		int zero; ///< Opaque stand-in for pixel value 0, which the hardware treats as transparent

		unsigned short* slotOf; ///< Slot holding each tile part, or NONE
		unsigned short* slotKey; ///< Tile part in each slot, or NONE
		unsigned short* slotRefs; ///< Map entries using each slot
		unsigned int* slotReleased; ///< Frame in which each slot was last released
		int clock; ///< Where the search for a free slot continues

		unsigned short map[3][32 * 32]; ///< Shadows of BG0, BG1 and BG2
		bool mapDirty[3];
		int windowX, windowY; ///< First map column and row on screen
		int oldWindowX, oldWindowY; ///< The window last frame
		int scrollX, scrollY;

		/// What each cell near the screen showed last frame, to skip unchanged ones
		struct Cell {
			short x, y, background, foreground;
			bool black;
		} cells[8][16];

		unsigned short overlaySlots[HUD_TOP * 32]; ///< Overlay slot of each cell, or NONE
		int overlayUsed;

		unsigned char* hud; ///< The 320x33 panel, scaled to the 240x26 shown, like the video surface
		int hudValues[16];
		bool hudDrawn; ///< Whether the panel has been drawn at all
		int hudWaited; ///< Frames a redraw of the panel has waited
		int hudLeft, hudRight; ///< Panel columns being redrawn
		SDL_Surface* hudSurface;

		SDL_Surface* spriteSurface;
		SDL_Surface* overlaySurface;
		int spritePriority;
		ObjEntry obj[OBJ_ENTRIES];
		int objHead; ///< Where sprite tiles are taken from next
		unsigned short oam[128 * 4]; ///< Sprites in drawing order
		int sprites;
		unsigned int frame;

		int skyFirst, skyScale;
		unsigned short skyTable[2][160]; ///< Backdrop colour of each scanline
		int skyShown; ///< Table the HBlank DMA uses

		/* Copies for VBlank */
		unsigned short oamOut[128 * 4];
		int spritesOut; ///< Sprites shown in oamOut; the rest are hidden
		int oamCopy; ///< Entries of oamOut that changed
		int scrollOutX, scrollOutY;

		SDL_Surface* oldCanvas;
		int oldCanvasW, oldCanvasH;
	};

	State* state = nullptr;

	/* While a level shows, the bitmap screen's pixels hold the state and its
	   largest tables. Keeping them out of the heap means the screen always
	   gets them back, however fragmented the heap has become. */
	unsigned char* screenMemory = nullptr;
	unsigned int screenUsed;

	/// Memory for the level display, from the screen's pixels while they last
	void* take (unsigned int size) {
		size = (size + 3) & ~3;

		if (screenMemory && SCREEN_WIDTH_NATIVE * SCREEN_HEIGHT_NATIVE - screenUsed >= size) {
			void* block = screenMemory + screenUsed;
			screenUsed += size;
			memset(block, 0, size);
			return block;
		}

		return calloc(size, 1);
	}

	void give (void* block) {
		unsigned char* bytes = static_cast<unsigned char*>(block);
		if (!screenMemory || bytes < screenMemory || bytes >= screenMemory + screenUsed) free(block);
	}


	/// Whether a word has a zero byte
	inline bool zeroByte (u32 v) {
		return (v - 0x01010101) & ~v & 0x80808080;
	}

	/// Pixel value for the hardware: transparent to 0, 0 to its stand-in
	inline u32 hardwarePixel (u32 p, u32 key, u32 transparent, u32 zero) {
		if (p == key) return transparent;
		if (p == 0) return zero;
		return p;
	}

	/// Eight pixels from any address, read as aligned words, which is faster than bytes
	inline void loadEight (const unsigned char* src, u32& a, u32& b) {
		uintptr_t address = reinterpret_cast<uintptr_t>(src);
		const u32* words = reinterpret_cast<const u32*>(address & ~3);
		int shift = (address & 3) << 3;
		a = words[0];
		b = words[1];
		if (shift) {
			u32 c = words[2];
			a = (a >> shift) | (b << (32 - shift));
			b = (b >> shift) | (c << (32 - shift));
		}
	}

	/// Four opaque pixels for the hardware: 0 to its stand-in
	inline u32 opaqueWord (u32 v, u32 zero) {
		if (!zeroByte(v)) return v;
		for (int shift = 0; shift < 32; shift += 8) {
			if (!(v & (0xFF << shift))) v |= zero << shift;
		}
		return v;
	}

	/// Convert four pixels for the hardware
	inline u32 hardwareWord (u32 v, u32 keys, u32 key, u32 transparent, u32 zero) {
		if (!zeroByte(v) && !zeroByte(v ^ keys)) return v;
		return hardwarePixel(v & 0xFF, key, transparent, zero) |
			(hardwarePixel((v >> 8) & 0xFF, key, transparent, zero) << 8) |
			(hardwarePixel((v >> 16) & 0xFF, key, transparent, zero) << 16) |
			(hardwarePixel(v >> 24, key, transparent, zero) << 24);
	}


	/// A slot to reuse, or -1 if all are in use
	OJ_FAST_CODE int freeSlot () {
		for (int i = 0; i < SLOTS; i++) {
			int s = state->clock;
			state->clock = (s + 1 < SLOTS)? s + 1: 2;

			// A slot released this frame may still be on show until the next VBlank
			if (s < 2 || state->slotRefs[s] || state->slotReleased[s] == state->frame) continue;

			if (state->slotKey[s] != NONE) {
				state->slotOf[state->slotKey[s]] = NONE;
				state->slotKey[s] = NONE;
			}

			return s;
		}

		return -1;
	}

	void release (int slot) {
		if (slot >= 2 && !--state->slotRefs[slot]) state->slotReleased[slot] = state->frame;
	}

	/// Drop the only reference to a panel or overlay slot
	void releaseOwned (int slot) {
		state->slotRefs[slot] = 0;
		state->slotReleased[slot] = state->frame;
	}

	/**
	 * Put a tile part into a slot.
	 *
	 * @param part Tile, 16 parts each, then which part, then 1 if over black
	 *
	 * @return The slot, with a reference taken
	 */
	OJ_FAST_CODE int acquire (int part) {
		int slot = state->slotOf[part];

		if (slot == NONE) {
			slot = freeSlot();
			if (slot < 0) {
				static bool warned = false;
				if (!warned) LOG_WARN("Out of tile slots");
				warned = true;
				return BLANK;
			}

			int tile = part >> 5;
			int sub = (part >> 1) & 15;
			bool overBlack = part & 1;
			const unsigned char* src = static_cast<const unsigned char*>(state->tileSet->pixels) +
				(((tile << 5) + ((sub >> 2) << 3)) << 5) + ((sub & 3) << 3);
			u32* dst = bgTiles + (slot << 4);

			u32 key = state->key;
			u32 keys = key * 0x01010101;
			u32 transparent = overBlack? state->black: 0;
			u32 zero = state->zero;
			u32 any = 0;

			for (int row = 0; row < 8; row++) {
				u32 a, b;
				if (reinterpret_cast<uintptr_t>(src) & 3) {
					a = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
					b = src[4] | (src[5] << 8) | (src[6] << 16) | (src[7] << 24);
				} else {
					a = reinterpret_cast<const u32*>(src)[0];
					b = reinterpret_cast<const u32*>(src)[1];
				}
				any |= (a ^ keys) | (b ^ keys);
				dst[0] = hardwareWord(a, keys, key, transparent, zero);
				dst[1] = hardwareWord(b, keys, key, transparent, zero);
				dst += 2;
				src += 32;
			}

			if (!any) {
				// Entirely transparent: no slot needed
				int shared = overBlack? BLACK: BLANK;
				state->slotOf[part] = shared;
				return shared;
			}

			state->slotOf[part] = slot;
			state->slotKey[slot] = part;
		}

		if (slot >= 2) state->slotRefs[slot]++;

		return slot;
	}

	/// Set one map entry of the foreground or background
	inline void setEntry (int layer, int index, int part) {
		unsigned short& entry = state->map[layer][index];
		int slot = (part < 0)? -part - 1: state->slotOf[part];

		if (slot == entry) return;

		release(entry);
		if (part >= 0) slot = acquire(part);
		else if (slot >= 2) state->slotRefs[slot]++;
		entry = slot;
		state->mapDirty[layer] = true;
	}


	/// Whether a frame still on screen may show an entry
	inline bool objShown (const ObjEntry& e) {
		return e.lastUsed + 2 > state->frame;
	}

	/// Free the entry that was drawn longest ago, if no frame on screen shows it
	bool evictObj () {
		int oldest = -1;
		for (int i = 0; i < OBJ_ENTRIES; i++) {
			ObjEntry& e = state->obj[i];
			if (!e.pixels || objShown(e)) continue;
			if (oldest < 0 || e.lastUsed < state->obj[oldest].lastUsed) oldest = i;
		}
		if (oldest < 0) return false;

		state->obj[oldest].pixels = nullptr;
		return true;
	}

	/**
	 * Take sprite tiles, the next ones round sprite VRAM like a ring, freeing
	 * the entries there. Entries a frame on screen shows are stepped over.
	 *
	 * @return The first tile, or -1 if there is no room
	 */
	OJ_FAST_CODE int takeObjTiles (int count) {
		int head = state->objHead;
		int travelled = 0;

		while (travelled < OBJ_UNITS * 2) {
			if (head + count > OBJ_UNITS) {
				travelled += OBJ_UNITS - head;
				head = 0;
			}

			// The end of the last shown entry in the way, if any
			int blocked = -1;
			for (int i = 0; i < OBJ_ENTRIES; i++) {
				const ObjEntry& e = state->obj[i];
				if (!e.pixels || e.tile >= head + count || e.tile + e.tiles <= head) continue;
				if (objShown(e) && e.tile + e.tiles > blocked) blocked = e.tile + e.tiles;
			}

			if (blocked < 0) {
				for (int i = 0; i < OBJ_ENTRIES; i++) {
					ObjEntry& e = state->obj[i];
					if (e.pixels && e.tile < head + count && e.tile + e.tiles > head) e.pixels = nullptr;
				}
				state->objHead = head + count;
				return head;
			}

			travelled += blocked - head;
			head = blocked;
		}

		return -1;
	}

	/// Hardware sprite shape and size for a piece, or -1 if there is none
	int pieceShape (int w, int h) {
		for (int shape = 0; shape < 3; shape++) {
			for (int size = 0; size < 4; size++) {
				if (objWidths[shape][size] == w && objHeights[shape][size] == h) return (shape << 2) | size;
			}
		}
		return -1;
	}

	/// Split a length into hardware sprite lengths, largest first
	int splitLength (int length, unsigned char* parts) {
		int n = 0;
		length = (length + 7) & ~7;
		while (length > 0) {
			int part = (length >= 64)? 64: (length >= 32)? 32: (length >= 16)? 16: 8;
			if (part > length) part = length;
			parts[n++] = part;
			length -= part;
		}
		return n;
	}

	/// Cover an image with hardware sprites
	void planPieces (ObjEntry& e) {
		unsigned char columns[16], rows[16];
		int nColumns = splitLength(e.w, columns);
		int nRows = splitLength(e.h, rows);

		e.pieces = 0;
		e.tiles = 0;

		int y = 0;
		for (int r = 0; r < nRows; r++) {
			int x = 0;
			for (int c = 0; c < nColumns; c++) {
				// Shapes like 64x8 do not exist, so split the longer side until one does
				int pw = columns[c], ph = rows[r];
				int stepX = pw, stepY = ph;
				while (pieceShape(stepX, stepY) < 0) {
					if (stepX > stepY) stepX >>= 1;
					else stepY >>= 1;
				}
				for (int py = 0; py < ph; py += stepY) {
					for (int px = 0; px < pw; px += stepX) {
						if (e.pieces == OBJ_PIECES) return;
						Piece& p = e.piece[e.pieces++];
						int shape = pieceShape(stepX, stepY);
						p.x = x + px;
						p.y = y + py;
						p.shape = shape >> 2;
						p.size = shape & 3;
						p.tile = e.tiles;
						e.tiles += (stepX >> 3) * (stepY >> 3);
					}
				}
				x += pw;
			}
			y += rows[r];
		}
	}

	/// Write an image's pixels into its sprite VRAM tiles
	OJ_FAST_CODE void uploadObj (const ObjEntry& e, const unsigned char* remap) {
		// Pixel values for the hardware: the key to transparent, 0 to its stand-in
		unsigned char lut[256];
		for (int v = 0; v < 256; v++) {
			int p = remap? remap[v]: v;
			lut[v] = p? p: state->zero;
		}
		if (e.key >= 0 && e.key < 256) lut[e.key] = 0;

		for (int i = 0; i < e.pieces; i++) {
			const Piece& p = e.piece[i];
			int pw = objWidths[p.shape][p.size], ph = objHeights[p.shape][p.size];
			u32* dst = objTiles + ((e.tile + p.tile) << 4);

			for (int ty = 0; ty < ph; ty += 8) {
				for (int tx = 0; tx < pw; tx += 8) {
					int sx = p.x + tx;
					int across = e.w - sx; // Pixels of the image in this tile row

					for (int row = 0; row < 8; row++, dst += 2) {
						int sy = p.y + ty + row;
						if (sy >= e.h) {
							dst[0] = dst[1] = 0;
							continue;
						}

						const unsigned char* src = e.pixels + sy * e.pitch + sx;

						if (across >= 8) {
							u32 a, b;
							loadEight(src, a, b);
							dst[0] = lut[a & 0xFF] | (lut[(a >> 8) & 0xFF] << 8) |
								(lut[(a >> 16) & 0xFF] << 16) | (lut[a >> 24] << 24);
							dst[1] = lut[b & 0xFF] | (lut[(b >> 8) & 0xFF] << 8) |
								(lut[(b >> 16) & 0xFF] << 16) | (lut[b >> 24] << 24);
						} else {
							u32 word[2] = {0, 0};
							for (int col = 0; col < across; col++)
								word[col >> 2] |= lut[src[col]] << ((col & 3) << 3);
							dst[0] = word[0];
							dst[1] = word[1];
						}
					}
				}
			}
		}
	}



	/// Slot for an overlay cell, allocated and cleared the first time this frame
	inline int overlaySlot (int column, int row) {
		unsigned short& slot = state->overlaySlots[row * 32 + column];

		if (slot == NONE) {
			int s = freeSlot();
			if (s < 0) return -1;
			state->slotRefs[s] = 1;
			u32* dst = bgTiles + (s << 4);
			for (int i = 0; i < 16; i++) dst[i] = 0;
			slot = s;
			state->map[0][row * 32 + column] = s;
			state->mapDirty[0] = true;
			state->overlayUsed++;
		}

		return slot;
	}

	/**
	 * The two words of a row of an overlay cell, allocating the cell if need be.
	 *
	 * @return The words, or nullptr if there is no slot for the cell
	 */
	inline u32* overlayRow (int x, int y) {
		int slot = overlaySlot(x >> 3, y >> 3);
		if (slot < 0) return nullptr;
		return bgTiles + (slot << 4) + ((y & 7) << 1);
	}

	/// 0xFF in each byte of a word that equals the same byte of another, 0 elsewhere
	inline u32 equalBytes (u32 v, u32 pattern) {
		u32 t = v ^ pattern;
		u32 high = ~(((t & 0x7F7F7F7F) + 0x7F7F7F7F) | t) & 0x80808080;
		return (high >> 7) * 0xFF;
	}

	/// Put a pixel value in one of the eight bytes of an overlay cell row
	inline void putByte (u32* row, int x, u32 p) {
		int shift = (x & 3) << 3;
		u32& word = row[(x >> 2) & 1];
		word = (word & ~(0xFF << shift)) | (p << shift);
	}

	/// Stands in for the pixels of surfaces that have none
	unsigned char noPixels;

	SDL_Surface* makeSurface (int w, int h, void* pixels, int pitch, Uint8 kind) {
		SDL_Surface* s = SDL_CreateRGBSurfaceFrom(pixels? pixels: &noPixels, w, h, 8, pitch, 0, 0, 0, 0);
		if (s) s->kind = kind;
		return s;
	}

}


void gbaLevel::enter (SDL_Surface* tileSet, int tiles, int key, int black) {
	if (state) return;

	// Nothing draws on the bitmap screen meanwhile, and its memory is needed
	gbaWaitFlip();
	screenMemory = gbaTakeScreen();
	screenUsed = 0;

	state = static_cast<State*>(take(sizeof(State)));
	if (!state) {
		LOG_ERROR("No memory for the tiled level display");
		gbaRestoreScreen(screenMemory);
		screenMemory = nullptr;
		return;
	}

	state->tileSet = tileSet;
	state->tiles = tiles;
	state->key = key;
	state->black = black;
	state->slotOf = static_cast<unsigned short*>(take(tiles * 32 * sizeof(unsigned short)));
	state->hud = static_cast<unsigned char*>(take(SCREEN_WIDTH_NATIVE * (SCREEN_HEIGHT_NATIVE - HUD_STORED_TOP)));
	state->slotReleased = static_cast<unsigned int*>(take(SLOTS * sizeof(unsigned int)));
	state->slotKey = static_cast<unsigned short*>(take(SLOTS * sizeof(unsigned short)));
	state->slotRefs = static_cast<unsigned short*>(take(SLOTS * sizeof(unsigned short)));

	/* The panel's surface spans the whole 320x200 canvas, but only its bottom
	   rows exist, and only the pixels shown, as on the video surface */
	state->hudSurface = makeSurface(320, 200, state->hud - HUD_STORED_TOP * SCREEN_WIDTH_NATIVE,
		SCREEN_WIDTH_NATIVE, SDL_GBA_NORMAL);
	state->spriteSurface = makeSurface(SCREEN_WIDTH_NATIVE, SCREEN_HEIGHT_NATIVE, nullptr, SCREEN_WIDTH_NATIVE, SDL_GBA_SPRITES);
	state->overlaySurface = makeSurface(SCREEN_WIDTH_NATIVE, SCREEN_HEIGHT_NATIVE, nullptr, SCREEN_WIDTH_NATIVE, SDL_GBA_OVERLAY);

	if (!state->slotOf || !state->slotKey || !state->slotRefs || !state->slotReleased || !state->hud ||
		!state->hudSurface || !state->spriteSurface || !state->overlaySurface) {
		LOG_ERROR("No memory for the tiled level display");
		leave();
		return;
	}

	state->hudSurface->bounds = {0, HUD_SOURCE_TOP, 320, 200 - HUD_SOURCE_TOP};
	state->hudSurface->clip_rect = state->hudSurface->bounds;
	state->overlaySurface->bounds = {0, 0, SCREEN_WIDTH_NATIVE, HUD_TOP * 8};
	state->overlaySurface->clip_rect = state->overlaySurface->bounds;

	memset(state->slotOf, 0xFF, tiles * 32 * sizeof(unsigned short));
	memset(state->slotKey, 0xFF, SLOTS * sizeof(unsigned short));
	memset(state->overlaySlots, 0xFF, sizeof(state->overlaySlots));
	state->clock = 2;
	state->spritePriority = 2;
	state->frame = 2;
	state->skyFirst = 0;
	state->windowX = -1000;

	// The opaque colour nearest to pixel value 0's
	SDL_Color* palette = video.getPalette();
	int bestDistance = 0x7FFFFFFF;
	for (int i = 1; i < 256; i++) {
		int dr = palette[i].r - palette[0].r;
		int dg = palette[i].g - palette[0].g;
		int db = palette[i].b - palette[0].b;
		int distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			state->zero = i;
		}
	}

	// Blank the display while VRAM is set up
	REG_DISPCNT = LCDC_OFF;
	DMA3COPY(state->map[0], VRAM, DMA32 | DMA_SRC_FIXED | (0x18000 / 4));
	u32 blackWord = black * 0x01010101;
	for (int i = 0; i < 16; i++) bgTiles[(BLACK << 4) + i] = blackWord;

	REG_BG0CNT = BG_256_COLOR | SCREEN_BASE(MAP_HUD) | BG_PRIORITY(0);
	REG_BG1CNT = BG_256_COLOR | SCREEN_BASE(MAP_FG) | BG_PRIORITY(1);
	REG_BG2CNT = BG_256_COLOR | SCREEN_BASE(MAP_BG) | BG_PRIORITY(2);
	REG_BG0HOFS = 0;
	REG_BG0VOFS = 0;

	// Frames only copy the sprites that changed, so hide them all to start
	for (int i = 0; i < 128; i++) state->oamOut[i * 4] = OBJ_HIDDEN;
	DMA3COPY(state->oamOut, oamMemory, DMA32 | (sizeof(state->oamOut) / 4));

	state->oldCanvas = canvas;
	state->oldCanvasW = canvasW;
	state->oldCanvasH = canvasH;
	canvas = state->overlaySurface;
	canvasW = SCREEN_WIDTH_NATIVE;
	canvasH = SCREEN_HEIGHT_NATIVE;
}


void gbaLevel::leave () {
	if (!state) return;

	gbaWaitFlip();

	REG_DISPCNT = LCDC_OFF;
	REG_DMA0CNT = 0;
	DMA3COPY(state->map[0], VRAM, DMA32 | DMA_SRC_FIXED | (0x14000 / 4));

	if (state->oldCanvas) {
		canvas = state->oldCanvas;
		canvasW = state->oldCanvasW;
		canvasH = state->oldCanvasH;
	}

	SDL_FreeSurface(state->hudSurface);
	SDL_FreeSurface(state->spriteSurface);
	SDL_FreeSurface(state->overlaySurface);
	give(state->slotOf);
	give(state->slotKey);
	give(state->slotRefs);
	give(state->slotReleased);
	give(state->hud);
	give(state);
	state = nullptr;

	if (!gbaRestoreScreen(screenMemory)) LOG_ERROR("No memory for the screen after the level");
	screenMemory = nullptr;

	// The bitmap screen returns at the next flip
	REG_DISPCNT = MODE_4 | BG2_ON;
}


bool gbaLevel::active () {
	return state;
}


void gbaLevel::beginFrame (int viewX, int viewY) {
	if (!state) return;

	// The shadows may only change once the last frame is on show
	gbaWaitFlip();

	state->frame++;
	state->sprites = 0;
	state->spritePriority = 2;

	// Clear the overlay; its slots stay shown until the next VBlank
	if (state->overlayUsed) {
		for (int i = 0; i < HUD_TOP * 32; i++) {
			if (state->overlaySlots[i] != NONE) {
				releaseOwned(state->overlaySlots[i]);
				state->overlaySlots[i] = NONE;
				state->map[0][i] = BLANK;
			}
		}
		state->overlayUsed = 0;
		state->mapDirty[0] = true;
	}

	state->scrollX = viewX;
	state->scrollY = viewY;
	state->oldWindowX = state->windowX;
	state->oldWindowY = state->windowY;
	state->windowX = viewX >> 3;
	state->windowY = viewY >> 3;
}


OJ_FAST_CODE void gbaLevel::setCell (int x, int y, int background, bool black, int foreground) {
	if (!state) return;

	State::Cell& cell = state->cells[y & 7][x & 15];
	bool same = cell.x == x && cell.y == y && cell.background == background &&
		cell.foreground == foreground && cell.black == black;

	// Map entries in the window last frame already show an unchanged cell
	int top = y << 2, left = x << 2;
	int wx = state->windowX, wy = state->windowY;
	int ox = state->oldWindowX, oy = state->oldWindowY;
	if (same) {
		int r0 = (top > wy)? top: wy, r1 = (top + 3 < wy + 20)? top + 3: wy + 20;
		int c0 = (left > wx)? left: wx, c1 = (left + 3 < wx + 30)? left + 3: wx + 30;
		if (r0 > r1 || c0 > c1 || (r0 >= oy && r1 <= oy + 20 && c0 >= ox && c1 <= ox + 30)) return;
	}

	cell.x = x;
	cell.y = y;
	cell.background = background;
	cell.foreground = foreground;
	cell.black = black;

	for (int j = 0; j < 4; j++) {
		int row = top + j;
		if (row < wy || row > wy + 20) continue;
		bool oldRow = same && row >= oy && row <= oy + 20;

		for (int i = 0; i < 4; i++) {
			int column = left + i;
			if (column < wx || column > wx + 30) continue;
			if (oldRow && column >= ox && column <= ox + 30) continue;

			int index = ((row & 31) << 5) + (column & 31);
			int sub = (j << 2) + i;

			// Negative parts name a fixed slot
			setEntry(2, index, (background >= 0)? (((background << 4) + sub) << 1) + black: (black? -BLACK - 1: -BLANK - 1));
			setEntry(1, index, (foreground >= 0)? ((foreground << 4) + sub) << 1: -BLANK - 1);
		}
	}
}


void gbaLevel::setSky (int first, int scale) {
	if (!state) return;

	state->skyFirst = first;
	state->skyScale = scale;
}


SDL_Surface* gbaLevel::spriteLayer () {
	return state? state->spriteSurface: canvas;
}


void gbaLevel::setSpritePriority (int priority) {
	if (state) state->spritePriority = priority;
}


SDL_Surface* gbaLevel::overlay () {
	return state? state->overlaySurface: canvas;
}


bool gbaLevel::beginHud (const HudField* fields, int count) {
	if (!state) return true;

	if (count > 16) count = 16;

	// The columns of the fields that changed
	int left = 320, right = 0;
	for (int i = 0; i < count; i++) {
		if (state->hudDrawn && fields[i].value == state->hudValues[i]) continue;
		if (fields[i].left < left) left = fields[i].left;
		if (fields[i].right > right) right = fields[i].right;
	}
	if (!state->hudDrawn) {
		left = 0;
		right = 320;
	}
	if (left >= right) return false;

	/* A redraw that would keep this frame from the next VBlank waits for a
	   frame or two. It takes about 32 scanlines plus one per three columns,
	   and the rest of the frame about 20. */
	int line = REG_VCOUNT;
	int linesLeft = (line < SCREEN_HEIGHT_NATIVE)? SCREEN_HEIGHT_NATIVE - line: 228 + SCREEN_HEIGHT_NATIVE - line;
	if (state->hudDrawn && state->hudWaited < 3 && linesLeft < 52 + ((right - left) / 3)) {
		state->hudWaited++;
		return false;
	}
	state->hudWaited = 0;
	state->hudDrawn = true;
	for (int i = 0; i < count; i++) state->hudValues[i] = fields[i].value;

	state->hudLeft = left;
	state->hudRight = right;
	state->hudSurface->bounds = {static_cast<Sint16>(left), HUD_SOURCE_TOP,
		static_cast<Uint16>(right - left), 200 - HUD_SOURCE_TOP};
	state->hudSurface->clip_rect = state->hudSurface->bounds;

	canvas = state->hudSurface;
	canvasW = 320;
	canvasH = 200;

	return true;
}


OJ_FAST_CODE void gbaLevel::endHud () {
	if (!state) return;

	canvas = state->overlaySurface;
	canvasW = SCREEN_WIDTH_NATIVE;
	canvasH = SCREEN_HEIGHT_NATIVE;

	int top = (HUD_TOP << 3) + (8 * 4 - HUD_HEIGHT);
	int first = scaledColumns(state->hudLeft) >> 3;
	int last = (scaledColumns(state->hudRight) + 7) >> 3;
	if (last > 30) last = 30;

	u32 zero = state->zero;

	// Give each changed panel cell a fresh slot, so the one on show stays intact
	for (int row = HUD_TOP; row < 20; row++) {
		for (int column = first; column < last; column++) {
			u32 words[16];

			for (int y = 0; y < 8; y++) {
				int screenY = (row << 3) + y;
				if (screenY < top) {
					words[y << 1] = words[(y << 1) + 1] = 0;
					continue;
				}
				const u32* src = reinterpret_cast<const u32*>(state->hud +
					(screenY - HUD_STORED_TOP) * SCREEN_WIDTH_NATIVE + (column << 3));
				words[y << 1] = opaqueWord(src[0], zero);
				words[(y << 1) + 1] = opaqueWord(src[1], zero);
			}

			unsigned short& entry = state->map[0][(row << 5) + column];
			if (entry >= 2) {
				const u32* shown = bgTiles + (entry << 4);
				int i = 0;
				while (i < 16 && shown[i] == words[i]) i++;
				if (i == 16) continue;
			}

			int slot = freeSlot();
			if (slot < 0) continue;
			u32* dst = bgTiles + (slot << 4);
			for (int i = 0; i < 16; i++) dst[i] = words[i];
			if (entry >= 2) releaseOwned(entry);
			state->slotRefs[slot] = 1;
			entry = slot;
			state->mapDirty[0] = true;
		}
	}
}


void gbaLevel::sprite (const unsigned char* pixels, int pitch, int w, int h, int x, int y,
	int key, const unsigned char* remap, unsigned int remapHash) {

	if (!state || w <= 0 || h <= 0) return;
	if (x >= SCREEN_WIDTH_NATIVE || y >= SCREEN_HEIGHT_NATIVE || x + w <= 0 || y + h <= 0) return;

	if (!remap) remapHash = 0;

	ObjEntry* e = nullptr;
	for (int i = 0; i < OBJ_ENTRIES; i++) {
		ObjEntry& o = state->obj[i];
		if (o.pixels == pixels && o.w == w && o.h == h && o.pitch == pitch && o.key == key && o.remapHash == remapHash) {
			e = &o;
			break;
		}
	}

	if (!e) {
		ObjEntry plan;
		plan.pixels = pixels;
		plan.w = w;
		plan.h = h;
		plan.pitch = pitch;
		plan.key = key;
		plan.remapHash = remapHash;
		planPieces(plan);

		// Room for its tiles, then a free entry, evicting what no shown frame uses
		int tile = takeObjTiles(plan.tiles);
		if (tile < 0) return;
		while (true) {
			for (int i = 0; i < OBJ_ENTRIES && !e; i++) {
				if (!state->obj[i].pixels) e = &state->obj[i];
			}
			if (e) break;
			if (!evictObj()) return;
		}

		*e = plan;
		e->tile = tile;
		uploadObj(*e, remap);
	}

	e->lastUsed = state->frame;

	for (int i = 0; i < e->pieces && state->sprites < 128; i++) {
		const Piece& p = e->piece[i];
		int px = x + p.x, py = y + p.y;
		if (px >= SCREEN_WIDTH_NATIVE || py >= SCREEN_HEIGHT_NATIVE ||
			px + objWidths[p.shape][p.size] <= 0 || py + objHeights[p.shape][p.size] <= 0) continue;

		unsigned short* a = state->oam + (state->sprites++ << 2);
		a[0] = (py & 0xFF) | OBJ_256_COLORS | (p.shape << 14);
		a[1] = (px & 0x1FF) | (p.size << 14);
		a[2] = ((e->tile + p.tile) << 1) | (state->spritePriority << 10);
	}
}


OJ_FAST_CODE void gbaLevel::overlayBlit (const unsigned char* pixels, int pitch, int x, int y, int w, int h,
	int key, const unsigned char* remap) {

	if (!state) return;

	u32 zero = state->zero;

	for (int row = 0; row < h; row++) {
		const unsigned char* src = pixels + row * pitch;

		// One cell row at a time, as VRAM takes no single bytes
		for (int col = 0; col < w;) {
			int sx = x + col;
			int end = col + 8 - (sx & 7);
			if (end > w) end = w;

			u32* dst = overlayRow(sx, y + row);
			if (dst && !remap && end - col == 8) {
				// A whole cell row, four pixels at a time
				u32 pixels[2];
				loadEight(src + col, pixels[0], pixels[1]);
				for (int i = 0; i < 2; i++) {
					u32 v = pixels[i];
					u32 keep = (key >= 0)? equalBytes(v, key * 0x01010101): 0;
					u32 zeros = equalBytes(v, 0);
					v = (v & ~zeros) | (zero * 0x01010101 & zeros);
					dst[i] = (dst[i] & keep) | (v & ~keep);
				}
			} else if (dst) {
				u32 words[2] = {dst[0], dst[1]};
				for (; col < end; col++, sx++) {
					u32 p = src[col];
					if (static_cast<int>(p) == key) continue;
					if (remap) p = remap[p];
					putByte(words, sx, p? p: zero);
				}
				dst[0] = words[0];
				dst[1] = words[1];
			}

			col = end;
		}
	}
}


OJ_FAST_CODE void gbaLevel::overlayFill (int x, int y, int w, int h, int color) {
	if (!state) return;

	u32 p = color? color: state->zero;

	for (int row = y; row < y + h; row++) {
		for (int col = x; col < x + w;) {
			int end = (col | 7) + 1;
			if (end > x + w) end = x + w;

			u32* dst = overlayRow(col, row);
			if (dst) {
				u32 words[2] = {dst[0], dst[1]};
				for (; col < end; col++) putByte(words, col, p);
				dst[0] = words[0];
				dst[1] = words[1];
			}

			col = end;
		}
	}
}


OJ_FAST_CODE void gbaLevel::commit (const unsigned short* palette) {
	if (!state) return;

	// Sprites drawn later go in front, which the hardware gives lower numbers
	int sprites = state->sprites;
	for (int i = 0; i < sprites; i++) {
		unsigned short* out = state->oamOut + (i << 2);
		const unsigned short* in = state->oam + ((sprites - 1 - i) << 2);
		out[0] = in[0];
		out[1] = in[1];
		out[2] = in[2];
	}
	for (int i = sprites; i < state->spritesOut; i++) state->oamOut[i << 2] = OBJ_HIDDEN;
	state->oamCopy = (sprites > state->spritesOut)? sprites: state->spritesOut;
	state->spritesOut = sprites;

	state->scrollOutX = state->scrollX;
	state->scrollOutY = state->scrollY;

	unsigned short* sky = state->skyTable[state->skyShown ^ 1];
	int index = state->skyFirst, step = 0;
	for (int y = 0; y < SCREEN_HEIGHT_NATIVE; y++) {
		sky[y] = palette[index & 255];
		if (state->skyScale && ++step == state->skyScale) {
			index++;
			step = 0;
		}
	}
}


void gbaLevel::vblank (bool committed) {
	if (!state) return;

	if (committed) {
		static const int blocks[3] = {MAP_HUD, MAP_FG, MAP_BG};
		for (int l = 0; l < 3; l++) {
			if (!state->mapDirty[l]) continue;
			DMA3COPY(state->map[l], SCREEN_BASE_BLOCK(blocks[l]), DMA32 | (sizeof(state->map[l]) / 4));
			state->mapDirty[l] = false;
		}
		if (state->oamCopy) DMA3COPY(state->oamOut, oamMemory, DMA32 | (state->oamCopy * 2));
		REG_BG1HOFS = state->scrollOutX;
		REG_BG1VOFS = state->scrollOutY;
		REG_BG2HOFS = state->scrollOutX;
		REG_BG2VOFS = state->scrollOutY;
		state->skyShown ^= 1;

		// The page bit means nothing in this mode, but toggling it shows each new frame to tools
		REG_DISPCNT = MODE_0 | BG0_ON | BG1_ON | BG2_ON | OBJ_ON | OBJ_1D_MAP | ((REG_DISPCNT ^ BACKBUFFER) & BACKBUFFER);
	}

	// The backdrop takes each scanline's sky colour during the HBlank before it
	const unsigned short* sky = state->skyTable[state->skyShown];
	REG_DMA0CNT = 0;
	BG_PALETTE[0] = sky[0];
	REG_DMA0SAD = reinterpret_cast<u32>(sky + 1);
	REG_DMA0DAD = reinterpret_cast<u32>(BG_PALETTE);
	REG_DMA0CNT = 1 | DMA_DST_FIXED | DMA_SRC_INC | DMA_REPEAT | DMA16 | DMA_HBLANK | DMA_ENABLE;
}
