
/**
 *
 * @file sweep.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _SWEEP_H
#define _SWEEP_H

#include <functional>

enum SweepType {
	SWEEP_MENU,   ///< Main menu and game menu screens
	SWEEP_LEVEL,  ///< LEVELx.yyy
	SWEEP_DEMO,   ///< MACRO.x demo levels
	SWEEP_BONUS,  ///< BONUSMAP.xxx
	SWEEP_PLANET, ///< PLANET.xxx approach sequences
	SWEEP_SCENE,  ///< *.0SC cutscenes
	SWEEP_TYPES
};

extern const char* const sweepTypeNames[SWEEP_TYPES];

/// Constructs an item and returns a function that deletes it. Throws an int
/// error code if the item fails to load.
using SweepLoad = std::function<std::function<void()>()>;

/// Called for every item found in the game directory, in alphabetical order
/// within each type
using SweepVisit = std::function<void(const char* name, SweepType type, const SweepLoad& load)>;

bool sweepGameData (const char* gameDir, const SweepVisit& visit);

#endif
