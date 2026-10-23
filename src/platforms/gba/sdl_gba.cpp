
/**
 *
 * @file sdl_gba.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Implements the SDL 1.2 stand-in (see sdl/SDL.h) on the Game Boy Advance.
 *
 * The video surface is an 8-bit buffer in EWRAM, copied to the hidden Mode 4
 * page at each flip. Byte writes do not work in VRAM, so the engine cannot
 * draw there directly. A 320x200 video surface is scaled down to the screen,
 * so screens laid out for 320x200 fit. The pages swap at the next VBlank,
 * together with the palette as it was at the flip.
 *
 */


#include <SDL.h>

#include "blit_gba.h"
#include "platforms/gba.h"

#include <gba_dma.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <gba_video.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 240
#define SCREEN_H 160
#define SCALED_W 320
#define SCALED_H 200

namespace {

	SDL_Surface* screen = nullptr;
	SDL_Color physPalette[256]; ///< Physical palette as given, for SDL_MapRGB
	u16 hwPalette[256]; ///< Physical palette in BGR555
	bool hwPaletteDirty = false;

	u16 flipPalette[256]; ///< Palette to show with the pending page
	volatile bool flipPaletteDirty = false;
	volatile bool flipPending = false; ///< A drawn page waits for VBlank

	const char* lastError = "";

	/// Button bit, key it is delivered as
	const struct { u16 button; SDLKey key; const char* name; } buttonMap[] = {
		{KEY_UP, SDLK_UP, "up"},
		{KEY_DOWN, SDLK_DOWN, "down"},
		{KEY_LEFT, SDLK_LEFT, "left"},
		{KEY_RIGHT, SDLK_RIGHT, "right"},
		{KEY_A, SDLK_SPACE, "a"},
		{KEY_B, SDLK_LALT, "b"},
		{KEY_L, SDLK_p, "l"},
		{KEY_R, SDLK_RCTRL, "r"},
		{KEY_START, SDLK_ESCAPE, "start"},
		{KEY_SELECT, SDLK_F9, "select"}
	};
	u16 reportedButtons = 0; ///< Buttons held down as far as the engine knows

	/* Button states sampled at each VBlank, so that a press shorter than one
	   of the engine's frames is not missed */
	const int BUTTON_LOG_SIZE = 16;
	volatile u16 buttonLog[BUTTON_LOG_SIZE];
	volatile unsigned int buttonLogHead = 0; ///< Written by the interrupt handler
	unsigned int buttonLogTail = 0;
	u16 sampledButtons = 0;

	/* Buttons pressed since SDL_PollEvent() last reported no more events. The
	   engine looks at which buttons are held once per frame, so their release
	   waits for the next frame. */
	u16 pressedThisFrame = 0;

	void onVBlank () {
		if (flipPending) {
			if (flipPaletteDirty) {
				dmaCopy(flipPalette, BG_PALETTE, sizeof(flipPalette));
				flipPaletteDirty = false;
			}
			REG_DISPCNT ^= BACKBUFFER;
			flipPending = false;
		}

		u16 held = ~REG_KEYINPUT & 0x3FF;
		if (held == sampledButtons) return;

		sampledButtons = held;
		if (buttonLogHead - buttonLogTail < BUTTON_LOG_SIZE) {
			buttonLog[buttonLogHead % BUTTON_LOG_SIZE] = held;
			buttonLogHead = buttonLogHead + 1;
		}
	}

	/**
	 * Intersect two rectangles.
	 *
	 * @return Whether the intersection is non-empty
	 */
	bool intersect (const SDL_Rect& a, const SDL_Rect& b, SDL_Rect& out) {
		int x0 = a.x > b.x? a.x: b.x;
		int y0 = a.y > b.y? a.y: b.y;
		int x1 = (a.x + a.w < b.x + b.w)? a.x + a.w: b.x + b.w;
		int y1 = (a.y + a.h < b.y + b.h)? a.y + a.h: b.y + b.h;

		if (x1 <= x0 || y1 <= y0) {
			out.x = x0; out.y = y0; out.w = out.h = 0;
			return false;
		}

		out.x = x0; out.y = y0; out.w = x1 - x0; out.h = y1 - y0;
		return true;
	}

	SDL_Surface* newSurface (int width, int height, void* pixels, int pitch) {
		SDL_Surface* s = static_cast<SDL_Surface*>(calloc(1, sizeof(SDL_Surface)));
		if (!s) {
			lastError = "Out of memory";
			return nullptr;
		}

		s->w = width;
		s->h = height;
		s->pitch = pitch;
		s->format = &s->formatData;
		s->formatData.BitsPerPixel = 8;
		s->formatData.BytesPerPixel = 1;
		s->clip_rect.w = width;
		s->clip_rect.h = height;

		if (pixels) {
			s->pixels = pixels;
			s->flags = SDL_PREALLOC;
		} else {
			s->pixels = malloc(pitch * height);
			if (!s->pixels) {
				struct mallinfo info = mallinfo();
				fprintf(stderr, "Surface %dx%d does not fit: %d bytes in use, %d free in heap\n",
					width, height, info.uordblks, info.fordblks);
				free(s);
				lastError = "Out of memory";
				return nullptr;
			}
		}

		return s;
	}

}


int SDL_Init (Uint32 flags) {
	(void)flags;

	irqInit();
	irqSet(IRQ_VBLANK, onVBlank);
	irqEnable(IRQ_VBLANK);

	// Milliseconds come from timers 2 and 3 cascaded, ticking at 16384 Hz
	REG_TM2CNT_H = 0;
	REG_TM3CNT_H = 0;
	REG_TM2CNT_L = 0;
	REG_TM3CNT_L = 0;
	REG_TM3CNT_H = TIMER_START | TIMER_COUNT;
	REG_TM2CNT_H = TIMER_START | 3;

	return 0;
}

void SDL_Quit (void) {
}

const char* SDL_GetError (void) {
	return lastError;
}

const SDL_version* SDL_Linked_Version (void) {
	static const SDL_version version = {1, 2, 0};
	return &version;
}


Uint32 SDL_GetTicks (void) {
	u16 high, low;

	// Read until no carry happened between the two halves
	do {
		high = REG_TM3CNT_L;
		low = REG_TM2CNT_L;
	} while (high != REG_TM3CNT_L);

	unsigned long long t = (static_cast<unsigned long long>(high) << 16) | low;
	return static_cast<Uint32>((t * 1000) >> 14);
}

void SDL_Delay (Uint32 ms) {
	Uint32 end = SDL_GetTicks() + ms;
	while (static_cast<Sint32>(end - SDL_GetTicks()) > 0);
}


SDL_Surface* SDL_SetVideoMode (int width, int height, int bpp, Uint32 flags) {
	(void)bpp;
	(void)flags;

	if (width != SCALED_W || height != SCALED_H) {
		width = SCREEN_W;
		height = SCREEN_H;
	}

	if (screen && (screen->w != width || screen->h != height)) {
		free(screen->pixels);
		free(screen);
		screen = nullptr;
	}

	if (!screen) {
		screen = newSurface(width, height, nullptr, width);
		if (!screen) return nullptr;
		memset(screen->pixels, 0, width * height);
	}

	SetMode(MODE_4 | BG2_ON);

	return screen;
}

SDL_Rect** SDL_ListModes (SDL_PixelFormat* format, Uint32 flags) {
	(void)format;
	(void)flags;

	static SDL_Rect scaled = {0, 0, SCALED_W, SCALED_H};
	static SDL_Rect native = {0, 0, SCREEN_W, SCREEN_H};
	static SDL_Rect* modes[] = {&scaled, &native, nullptr};
	return modes;
}

int SDL_Flip (SDL_Surface* surface) {
	if (surface != screen) return -1;

	gbaCheckStack();

	// The page drawn last time must be on show before drawing the other one
	while (flipPending) VBlankIntrWait();

	u16* page = reinterpret_cast<u16*>((REG_DISPCNT & BACKBUFFER)? VRAM: VRAM + 0xA000);

	if (screen->w == SCALED_W) {
		gbaScaleFrame(static_cast<const u8*>(screen->pixels), page);
	} else {
		DMA3COPY(screen->pixels, page, DMA32 | (SCREEN_W * SCREEN_H / 4));
	}

	if (hwPaletteDirty) {
		memcpy(flipPalette, hwPalette, sizeof(flipPalette));
		flipPaletteDirty = true;
		hwPaletteDirty = false;
	}

	flipPending = true;

	return 0;
}

SDL_Surface* SDL_CreateRGBSurface (Uint32 flags, int width, int height, int depth,
	Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
	(void)flags;
	(void)Rmask;
	(void)Gmask;
	(void)Bmask;
	(void)Amask;

	if (depth != 8) {
		lastError = "Only 8-bit surfaces are supported";
		return nullptr;
	}

	return newSurface(width, height, nullptr, width);
}

SDL_Surface* SDL_CreateRGBSurfaceFrom (void* pixels, int width, int height, int depth, int pitch,
	Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
	(void)Rmask;
	(void)Gmask;
	(void)Bmask;
	(void)Amask;

	if (depth != 8) {
		lastError = "Only 8-bit surfaces are supported";
		return nullptr;
	}

	return newSurface(width, height, pixels, pitch);
}

void SDL_FreeSurface (SDL_Surface* surface) {
	if (!surface || surface == screen) return;

	if (!(surface->flags & SDL_PREALLOC)) free(surface->pixels);
	free(surface->remap);
	free(surface);
}

int SDL_LockSurface (SDL_Surface* surface) {
	(void)surface;
	return 0;
}

void SDL_UnlockSurface (SDL_Surface* surface) {
	(void)surface;
}

int SDL_SetPalette (SDL_Surface* surface, int flags, const SDL_Color* colors, int first, int ncolors) {
	if (!surface) return 0;

	if (first < 0 || ncolors <= 0 || first + ncolors > 256) return 0;

	if (surface == screen) {
		// The video surface's logical palette is always the identity
		if (flags & SDL_PHYSPAL) {
			for (int i = 0; i < ncolors; i++) {
				const SDL_Color& c = colors[i];
				physPalette[first + i] = c;
				hwPalette[first + i] = RGB5(c.r >> 3, c.g >> 3, c.b >> 3);
			}
			hwPaletteDirty = true;
		}
		return 1;
	}

	if (!(flags & SDL_LOGPAL)) return 1;

	if (!surface->remap) {
		bool identity = true;
		for (int i = 0; i < ncolors && identity; i++)
			identity = colors[i].r == first + i;
		if (identity) return 1;

		surface->remap = static_cast<Uint8*>(malloc(256));
		if (!surface->remap) return 0;
		for (int i = 0; i < 256; i++) surface->remap[i] = i;
	}

	for (int i = 0; i < ncolors; i++) surface->remap[first + i] = colors[i].r;

	// Drop the table once it is back to the identity, so blits take the fast path
	for (int i = 0; i < 256; i++) {
		if (surface->remap[i] != i) return 1;
	}
	free(surface->remap);
	surface->remap = nullptr;

	return 1;
}

int SDL_SetColors (SDL_Surface* surface, const SDL_Color* colors, int first, int ncolors) {
	return SDL_SetPalette(surface, SDL_LOGPAL | SDL_PHYSPAL, colors, first, ncolors);
}

int SDL_SetColorKey (SDL_Surface* surface, Uint32 flag, Uint32 key) {
	if (flag & SDL_SRCCOLORKEY) {
		surface->flags |= SDL_SRCCOLORKEY;
		surface->format->colorkey = key;
	} else {
		surface->flags &= ~SDL_SRCCOLORKEY;
	}
	return 0;
}

SDL_bool SDL_SetClipRect (SDL_Surface* surface, const SDL_Rect* rect) {
	SDL_Rect full = {0, 0, static_cast<Uint16>(surface->w), static_cast<Uint16>(surface->h)};

	if (!rect) {
		surface->clip_rect = full;
		return SDL_TRUE;
	}

	return intersect(*rect, full, surface->clip_rect)? SDL_TRUE: SDL_FALSE;
}

int SDL_BlitSurface (SDL_Surface* src, SDL_Rect* srcrect, SDL_Surface* dst, SDL_Rect* dstrect) {
	SDL_Rect full = {0, 0, static_cast<Uint16>(src->w), static_cast<Uint16>(src->h)};
	SDL_Rect s;

	// Source area, clipped to the source surface
	if (srcrect) {
		if (!intersect(*srcrect, full, s)) {
			if (dstrect) dstrect->w = dstrect->h = 0;
			return 0;
		}
	} else {
		s = full;
	}

	int dx = dstrect? dstrect->x: 0;
	int dy = dstrect? dstrect->y: 0;
	if (srcrect) {
		// Keep the destination aligned with the source when it was clipped
		dx += s.x - srcrect->x;
		dy += s.y - srcrect->y;
	}

	SDL_Rect d = {static_cast<Sint16>(dx), static_cast<Sint16>(dy), s.w, s.h};
	SDL_Rect c;
	if (!intersect(d, dst->clip_rect, c)) {
		if (dstrect) {
			dstrect->w = dstrect->h = 0;
		}
		return 0;
	}

	s.x += c.x - d.x;
	s.y += c.y - d.y;

	if (dstrect) *dstrect = c;

	gbaBlit(static_cast<const Uint8*>(src->pixels) + s.y * src->pitch + s.x, src->pitch,
		static_cast<Uint8*>(dst->pixels) + c.y * dst->pitch + c.x, dst->pitch, c.w, c.h,
		(src->flags & SDL_SRCCOLORKEY)? static_cast<int>(src->format->colorkey & 0xFF): -1, src->remap);

	return 0;
}

int SDL_FillRect (SDL_Surface* dst, SDL_Rect* rect, Uint32 color) {
	SDL_Rect c;

	if (rect) {
		if (!intersect(*rect, dst->clip_rect, c)) {
			*rect = c;
			return 0;
		}
		*rect = c;
	} else {
		c = dst->clip_rect;
	}

	Uint8* dp = static_cast<Uint8*>(dst->pixels) + c.y * dst->pitch + c.x;
	for (int y = 0; y < c.h; y++) {
		memset(dp, color, c.w);
		dp += dst->pitch;
	}

	return 0;
}

Uint32 SDL_MapRGB (const SDL_PixelFormat* format, Uint8 r, Uint8 g, Uint8 b) {
	(void)format;

	// Nearest colour of the physical palette
	Uint32 best = 0;
	int bestDistance = 0x7FFFFFFF;
	for (int i = 0; i < 256; i++) {
		int dr = physPalette[i].r - r;
		int dg = physPalette[i].g - g;
		int db = physPalette[i].b - b;
		int distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
			if (!distance) break;
		}
	}

	return best;
}

void SDL_WM_SetCaption (const char* title, const char* icon) {
	(void)title;
	(void)icon;
}

int SDL_ShowCursor (int toggle) {
	(void)toggle;
	return SDL_DISABLE;
}


int SDL_PollEvent (SDL_Event* event) {
	u16 held, changed;

	// Catch up with the sampled states in order, then follow the buttons
	while (true) {
		bool logged = buttonLogTail != buttonLogHead;
		held = logged? buttonLog[buttonLogTail % BUTTON_LOG_SIZE]: ~REG_KEYINPUT & 0x3FF;
		changed = held ^ reportedButtons;

		if (changed) break;
		if (!logged) {
			pressedThisFrame = 0;
			return 0;
		}
		buttonLogTail++;
	}

	if (changed & ~held & pressedThisFrame) {
		pressedThisFrame = 0;
		return 0;
	}

	// Report one change per call
	for (const auto& entry: buttonMap) {
		if (!(changed & entry.button)) continue;

		reportedButtons ^= entry.button;
		if (held & entry.button) pressedThisFrame |= entry.button;

		if (event) {
			memset(event, 0, sizeof(SDL_Event));
			event->type = (held & entry.button)? SDL_KEYDOWN: SDL_KEYUP;
			event->key.type = event->type;
			event->key.state = (held & entry.button)? 1: 0;
			event->key.keysym.sym = entry.key;
		}

		return 1;
	}

	return 0;
}

char* SDL_GetKeyName (SDLKey key) {
	for (const auto& entry: buttonMap) {
		if (entry.key == key) return const_cast<char*>(entry.name);
	}

	return const_cast<char*>("");
}


int SDL_NumJoysticks (void) {
	return 0;
}

SDL_Joystick* SDL_JoystickOpen (int index) {
	(void)index;
	return nullptr;
}

void SDL_JoystickClose (SDL_Joystick* joystick) {
	(void)joystick;
}
