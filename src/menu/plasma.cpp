
/**
 *
 * @file plasma.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd June 2010: Created plasma.cpp
 *
 * @par Licence:
 * Copyright (c) 2010 Alireza Nejati
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Cool plasma effects for the main menu.
 *
 */


#include "plasma.h"

#include <cstdint>

#include "SDL_wrapper.h"
#include "level/level.h"
#include "util.h"
#include "io/gfx/video.h"
#include "loop.h"


/**
 * Create the plasma.
 */
Plasma::Plasma(){

	p0=0;
	p1=0;
	p2=0;
	p3=0;
	lastTicks=0;

	//fSin, fCos: pi = 512
	// -1024 < out < 1024
}

namespace {

	/* A pixel is bits 10 to 13 of colb + cols[x], so only both values modulo
	   2^14 matter. Two of them fit in the halves of a word without carrying
	   into each other, so one addition makes two pixels. Columns are paired so
	   that the pixels of a group of four come out in memory order. */

	/// Two columns' values, in the halves of a word
	inline uint32_t columnPair (unsigned int low, unsigned int high) {
		return (low & 0x3FFF) | ((high & 0x3FFF) << 16);
	}

	/// Four plasma pixels as they lie in memory
	inline uint32_t plasmaWord (uint32_t rowPair, uint32_t pairA, uint32_t pairB) {
		uint32_t a = ((rowPair + pairA) >> 10) & 0x000F000F;
		uint32_t b = ((rowPair + pairB) >> 10) & 0x000F000F;
		return a | (b << 8);
	}

}

/**
 * Draw the plasma.
 *
 * An overlay is drawn over the plasma with index 0 transparent, in the same
 * pass when the memory layout allows, which saves computing plasma that the
 * overlay hides.
 *
 * @param overlay Surface to draw over the plasma, or nullptr
 * @param overlayX X-coordinate of the overlay on the canvas
 * @param overlayY Y-coordinate of the overlay on the canvas
 *
 * @return Error code
 */
OJ_FAST_CODE int Plasma::draw(SDL_Surface *overlay, int overlayX, int overlayY){
	int x,y;

	int w,h,pitch;
	unsigned char *px;
	unsigned char colour;

	// draw plasma

	SDL_LockSurface(canvas);

	// Stored columns and rows, which may be fewer than the canvas has
	w 		= canvasColumns(canvas, canvas->w);
	h 		= canvasRows(canvas, canvas->h);
	pitch 	= canvas->pitch;

	px = static_cast<unsigned char*>(canvas->pixels);

	// The part that depends on x is the same for every row. Kept on the stack
	// when it fits, which is fast memory on some platforms
	unsigned int stackColumns[SW];
	unsigned int *cols = stackColumns;
	if (w > SW) {
		if (columns.size() < static_cast<size_t>(w)) columns.resize(w);
		cols = columns.data();
	}

	for(x=0;x<w;x++){
		int cx = canvasColumnOf(canvas, x);
		cols[x] = (fCos((p2 + (cx * 3))*4)<<3)+(fCos((p3 + (cx * 2))*4)<<3);
	}

	// Rows can be written four pixels at a time when they are word aligned
	bool words = !(reinterpret_cast<uintptr_t>(px) & 3) && !(pitch & 3) && !(w & 3);

	// Column pairs for each group of four pixels
	uint32_t stackPairs[SW / 2];
	uint32_t *pairs = stackPairs;
	if (words) {
		if (w > SW) {
			if (columnPairs.size() < static_cast<size_t>(w / 2)) columnPairs.resize(w / 2);
			pairs = columnPairs.data();
		}
		for(x=0;x<w;x+=4){
#if WORDS_BIGENDIAN
			pairs[x >> 1] = columnPair(cols[x+3], cols[x+1]);
			pairs[(x >> 1) + 1] = columnPair(cols[x+2], cols[x]);
#else
			pairs[x >> 1] = columnPair(cols[x], cols[x+2]);
			pairs[(x >> 1) + 1] = columnPair(cols[x+1], cols[x+3]);
#endif
		}
	}

	// The overlay can be merged when it is word aligned and inside the canvas
	const unsigned char *overlayPixels = nullptr;
	int overlayPitch = 0, overlayW = 0, overlayH = 0;
	if (overlay) {
		if (SDL_MUSTLOCK(overlay)) SDL_LockSurface(overlay);
		overlayPixels = static_cast<const unsigned char*>(overlay->pixels);
		overlayPitch = overlay->pitch;
		overlayW = overlay->w;
		overlayH = overlay->h;
	}
	bool merge = words && overlayPixels && w == canvas->w && h == canvas->h && !(reinterpret_cast<uintptr_t>(overlayPixels) & 3) &&
		!(overlayPitch & 3) && !(overlayW & 3) && !(overlayX & 3) &&
		overlayX >= 0 && overlayY >= 0 && overlayX + overlayW <= w && overlayY + overlayH <= h;

    for(y=0;y<h;y++){
		int cy = canvasRowOf(canvas, y);
		unsigned int colb = (fCos((p0 + (cy * 2))*4)<<3)+(fCos((p1 + cy)*4)<<3)+(32<<10);
		if (words) {
			uint32_t rowPair = columnPair(colb, colb);
			const uint32_t *pair = pairs;
			uint32_t *pw = reinterpret_cast<uint32_t*>(px);
			int start = w, end = w;
			if (merge && y >= overlayY && y < overlayY + overlayH) {
				start = overlayX;
				end = overlayX + overlayW;
			}

			for(x=0;x<start;x+=4){
				*pw++ = plasmaWord(rowPair, pair[0], pair[1]);
				pair += 2;
			}

			const uint32_t *ow = reinterpret_cast<const uint32_t*>(
				overlayPixels + (y - overlayY) * overlayPitch);
			for(;x<end;x+=4){
				uint32_t o = *ow++;

				// 0x80 in each byte of the overlay that is not transparent
				uint32_t opaque = (((o & 0x7F7F7F7F) + 0x7F7F7F7F) | o) & 0x80808080;

				if (opaque == 0x80808080) {
					*pw++ = o;
				} else {
					uint32_t mask = (opaque >> 7) * 0xFF;
					*pw++ = (o & mask) | (plasmaWord(rowPair, pair[0], pair[1]) & ~mask);
				}
				pair += 2;
			}

			for(;x<w;x+=4){
				*pw++ = plasmaWord(rowPair, pair[0], pair[1]);
				pair += 2;
			}
		} else {
			for(x=0;x<w;x++){

				colour = ((colb+cols[x])>>10) & 0xF;

				px[x] = colour;
			}
		}
		// go to next row
		px += pitch;
	}

	// Move on one step per T_PLASMA_STEP, so the plasma keeps its speed where
	// frames take longer, and by at least one step per frame
	int steps = 1;
	if (lastTicks) {
		unsigned int elapsed = globalTicks - lastTicks;
		if (elapsed >= 2 * T_PLASMA_STEP) steps = elapsed / T_PLASMA_STEP;
		if (steps > MAX_PLASMA_STEPS) steps = MAX_PLASMA_STEPS;
	}
	lastTicks = globalTicks;

	for (int i = 0; i < steps; i++) {
		p0 = p0 < 256 ? p0+1 : 1;
		p1 = p1 < 256 ? p1+2 : 2;
		p2 = p2 < 256 ? p2+3 : 3;
		p3 = p3 < 256 ? p3+4 : 4;
	}

	SDL_UnlockSurface(canvas);

	if (overlay) {
		if (SDL_MUSTLOCK(overlay)) SDL_UnlockSurface(overlay);

		if (!merge) {
			SDL_Rect dst = {static_cast<decltype(dst.x)>(overlayX), static_cast<decltype(dst.y)>(overlayY), 0, 0};
			SDL_BlitSurface(overlay, nullptr, canvas, &dst);
		}
	}

	return E_NONE;

}
