
/**
 *
 * @file plasma.h
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd June 2010: Created plasma.h
 *
 * @par Licence:
 * Copyright (c) 2010 Alireza Nejati
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */

#ifndef _PLASMA_H
#define _PLASMA_H

#include <cstdint>
#include <vector>

#include "SDL_wrapper.h"

#define T_PLASMA_STEP 20 ///< Time per step of the plasma's movement
#define MAX_PLASMA_STEPS 4 ///< Most steps the plasma moves in one frame

/// Main menu background plasma effect
class Plasma {

	private:
		int p0,p1,p2,p3;
		unsigned int lastTicks; ///< Time of the last frame, or 0 before the first
		std::vector<unsigned int> columns; ///< Per-column part of the current frame
		std::vector<uint32_t> columnPairs; ///< The same, two columns to a word

	public:
		Plasma ();

		int draw(SDL_Surface *overlay = nullptr, int overlayX = 0, int overlayY = 0);

};

#endif

