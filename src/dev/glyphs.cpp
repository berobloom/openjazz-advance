
/**
 *
 * @file glyphs.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Shrinks font characters for screens that show the 320x200 canvas at
 * 240x160 (OJ_SCALED_CANVAS). Dropping every fourth column and fifth row
 * loses whichever strokes fall there, so "R" becomes "N". Instead, adjacent
 * columns (then rows) are merged one pair at a time, choosing the pair whose
 * merge loses least, and a merged pixel keeps any stroke from either side.
 *
 */


#include "glyphs.h"

#include <cstddef>
#include <vector>

namespace {

	using Line = std::vector<unsigned char>;

	// Weights of what a merge loses
	const int SHAPE_COST = 2; ///< Per pixel that changes between stroke and background
	const int SHADE_COST = 1; ///< Per pixel that changes value
	const int GAP_COST = 10; ///< Per gap one line wide that closes, like the inside of an "8"

	/// What merging two neighbouring lines loses
	int cost (const std::vector<Line>& lines, size_t i, int background) {

		const Line& a = lines[i];
		const Line& b = lines[i + 1];
		int c = 0;

		for (size_t p = 0; p < a.size(); p++) {
			if (a[p] == b[p]) continue;
			c += SHADE_COST;

			bool aBack = a[p] == background, bBack = b[p] == background;
			if (aBack == bBack) continue;
			c += SHAPE_COST;

			// The merge fills the background pixel. Does it close a gap?
			size_t lo = aBack? i: i + 1, hi = lo;
			if (lo > 0 && lines[lo - 1][p] == background) continue;
			if (hi + 1 < lines.size() && lines[hi + 1][p] == background) continue;
			// A margin at the edge is not a gap
			if (lo > 0 && hi + 1 < lines.size()) c += GAP_COST;
		}

		return c;

	}

	/// Merge neighbouring lines until only the given number remain
	void shrink (std::vector<Line>& lines, size_t count, int background) {

		while (lines.size() > count) {

			size_t best = 0;
			int bestCost = -1;

			for (size_t i = 0; i + 1 < lines.size(); i++) {
				int c = cost(lines, i, background);
				if (bestCost < 0 || c < bestCost) {
					best = i;
					bestCost = c;
				}
			}

			// A stroke on either side survives
			Line& a = lines[best];
			const Line& b = lines[best + 1];
			for (size_t p = 0; p < a.size(); p++)
				if (a[p] == background) a[p] = b[p];

			lines.erase(lines.begin() + best + 1);

		}

	}

}


int shrunkGlyphWidth (int width) {

	return ((3 * width) + 2) / 4;

}


int shrunkGlyphHeight (int height) {

	return ((4 * height) + 2) / 5;

}


void shrinkGlyph (const unsigned char* src, int pitch, int width, int height,
	int background, unsigned char* dst) {

	int w = shrunkGlyphWidth(width);
	int h = shrunkGlyphHeight(height);

	std::vector<Line> columns(width, Line(height));
	for (int x = 0; x < width; x++)
		for (int y = 0; y < height; y++) columns[x][y] = src[(y * pitch) + x];

	shrink(columns, w, background);

	std::vector<Line> rows(height, Line(w));
	for (int y = 0; y < height; y++)
		for (int x = 0; x < w; x++) rows[y][x] = columns[x][y];

	shrink(rows, h, background);

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) dst[(y * w) + x] = rows[y][x];

}
