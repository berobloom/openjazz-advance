
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
 * Scale a 320x200 frame to the 240x160 screen, keeping three of every four
 * columns and four of every five rows.
 *
 * @param src 320x200 8-bit pixels, word aligned
 * @param dst Mode 4 frame buffer page
 */
void gbaScaleFrame (const uint8_t* src, uint16_t* dst);

#endif
