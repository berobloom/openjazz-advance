
/**
 *
 * @file blit_gba.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Pixel loops of the SDL stand-in, run from IWRAM as ARM code.
 *
 */


#ifndef OJ_GBA_BLIT_H
#define OJ_GBA_BLIT_H

#include <stdint.h>

/**
 * Copy a block of 8-bit pixels.
 *
 * @param src First source pixel
 * @param srcPitch Source bytes per row
 * @param dst First destination pixel
 * @param dstPitch Destination bytes per row
 * @param w Pixels per row
 * @param h Rows
 * @param key Source value to skip, or -1 to copy every pixel
 * @param remap Table applied to copied pixels, or nullptr
 */
void gbaBlit (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
	int w, int h, int key, const uint8_t* remap);

/**
 * Copy a block of 8-bit pixels to a scaled surface, which keeps only columns
 * 0, 1 and 3 of every four and rows 0, 1, 3 and 4 of every five.
 *
 * @param src Source pixel for the block's first column and row
 * @param srcPitch Source bytes per row
 * @param dst First pixel of the scaled surface
 * @param dstPitch Scaled surface bytes per row
 * @param x Block's first column, in full-size coordinates
 * @param y Block's first row, in full-size coordinates
 * @param w Block width, in full-size pixels
 * @param h Block height, in full-size pixels
 * @param key Source value to skip, or -1 to copy every pixel
 * @param remap Table applied to copied pixels, or nullptr
 */
void gbaBlitScaled (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
	int x, int y, int w, int h, int key, const uint8_t* remap);

/**
 * Fill a block of 8-bit pixels.
 *
 * @param dst First pixel
 * @param pitch Bytes per row
 * @param w Pixels per row
 * @param h Rows
 * @param color Pixel value
 */
void gbaFill (uint8_t* dst, int pitch, int w, int h, uint8_t color);

/**
 * Copy words with the CPU. Unlike DMA, this lets interrupts in, so it does not
 * delay Maxmod's VBlank handler.
 *
 * @param src Source, word-aligned
 * @param dst Destination, word-aligned
 * @param words Number of words, a multiple of 8
 */
void gbaCopyWords (const void* src, void* dst, int words);

/**
 * Fill words with the CPU, letting interrupts in (see gbaCopyWords).
 *
 * @param dst Destination, word-aligned
 * @param value Word to store
 * @param words Number of words, a multiple of 8
 */
void gbaFillWords (void* dst, uint32_t value, int words);

#endif
