
/**
 *
 * @file sweep.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Loads every menu, level, demo, bonus level, planet and cutscene in the game
 * directory in turn, for the developer tools that need to see all of them.
 *
 */


#include "sweep.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "game/game.h"
#include "jj1/bonuslevel/jj1bonuslevel.h"
#include "jj1/level/jj1level.h"
#include "jj1/planet/jj1planet.h"
#include "jj1/scene/jj1scene.h"
#include "menu/menu.h"
#include "io/log.h"

const char* const sweepTypeNames[SWEEP_TYPES] = {
	"Menus", "Levels", "Demos", "Bonus levels", "Planets", "Cutscenes"
};

/// Whether name is prefix followed by exactly the given number of digits
static bool matches (const char* name, const char* prefix, int digits) {

	size_t len = strlen(prefix);

	if (strncasecmp(name, prefix, len) != 0) return false;
	if (strlen(name) != len + digits) return false;

	for (int i = 0; i < digits; i++)
		if (!isdigit(static_cast<unsigned char>(name[len + i]))) return false;

	return true;

}

static bool classify (const char* name, SweepType& type) {

	size_t len = strlen(name);

	// LEVELx.yyy, where x is the level and yyy the world
	if (len == 10 && strncasecmp(name, "LEVEL", 5) == 0 &&
		isdigit(static_cast<unsigned char>(name[5])) && name[6] == '.' &&
		matches(name + 7, "", 3)) {
		type = SWEEP_LEVEL;
	} else if (matches(name, "MACRO.", 1)) {
		type = SWEEP_DEMO;
	} else if (matches(name, "BONUSMAP.", 3)) {
		type = SWEEP_BONUS;
	} else if (matches(name, "PLANET.", 3)) {
		type = SWEEP_PLANET;
	} else if (len > 4 && strcasecmp(name + len - 4, ".0SC") == 0) {
		type = SWEEP_SCENE;
	} else {
		return false;
	}

	return true;

}

bool sweepGameData (const char* gameDir, const SweepVisit& visit) {

	std::vector<std::string> names[SWEEP_TYPES];

	DIR* dir = opendir(gameDir);
	if (!dir) {
		LOG_ERROR("sweep: cannot open '%s'", gameDir);
		return false;
	}

	while (struct dirent* entry = readdir(dir)) {
		SweepType type;
		if (classify(entry->d_name, type)) names[type].push_back(entry->d_name);
	}

	closedir(dir);

	for (auto& list : names)
		std::sort(list.begin(), list.end(),
			[](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });

	LocalGame game("LEVEL0.000", difficultyType::Normal);

	visit("MENU.000", SWEEP_MENU, []() {
		MainMenu* menu = new MainMenu();
		return [menu]() { delete menu; };
	});

	for (int type = SWEEP_LEVEL; type < SWEEP_TYPES; type++) {

		for (std::string& entry : names[type]) {

			char* name = &entry[0];

			switch (type) {

				case SWEEP_LEVEL:
					visit(name, SWEEP_LEVEL, [&]() {
						JJ1Level* level = new JJ1Level(&game, name, false, false);
						return [level]() { delete level; };
					});
					break;

				case SWEEP_DEMO:
					visit(name, SWEEP_DEMO, [&]() {
						JJ1DemoLevel* level = new JJ1DemoLevel(&game, name);
						return [level]() { delete level; };
					});
					break;

				case SWEEP_BONUS:
					visit(name, SWEEP_BONUS, [&]() {
						JJ1BonusLevel* level = new JJ1BonusLevel(&game, name, false);
						return [level]() { delete level; };
					});
					break;

				case SWEEP_PLANET:
					visit(name, SWEEP_PLANET, [&]() {
						JJ1Planet* planet = new JJ1Planet(name, -1);
						return [planet]() { delete planet; };
					});
					break;

				case SWEEP_SCENE:
					visit(name, SWEEP_SCENE, [&]() {
						JJ1Scene* scene = new JJ1Scene(name);
						return [scene]() { delete scene; };
					});
					break;

			}

		}

	}

	return true;

}
