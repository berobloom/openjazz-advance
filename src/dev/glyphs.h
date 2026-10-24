
/**
 *
 * @file glyphs.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Shrinks font characters for screens that show the 320x200 canvas at
 * 240x160 (OJ_SCALED_CANVAS), keeping their strokes.
 *
 */


#ifndef _GLYPHS_H
#define _GLYPHS_H

/// Width of a character shrunk for a scaled screen
int shrunkGlyphWidth (int width);

/// Height of a character shrunk for a scaled screen
int shrunkGlyphHeight (int height);

/**
 * Shrink a character to shrunkGlyphWidth() by shrunkGlyphHeight() pixels.
 *
 * @param src First pixel of the character
 * @param pitch Source bytes per row
 * @param width Source width
 * @param height Source height
 * @param background Value of the pixels around the strokes
 * @param dst Destination, shrunk width bytes per row
 */
void shrinkGlyph (const unsigned char* src, int pitch, int width, int height,
	int background, unsigned char* dst);

#endif
