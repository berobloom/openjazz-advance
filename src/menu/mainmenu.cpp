
/**
 *
 * @file mainmenu.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd of August 2005: Created menu.c
 * - 3rd of February 2009: Renamed menu.c to menu.cpp
 * - 19th July 2009: Created menumain.cpp from parts of menu.cpp
 * - 26th July 2009: Renamed menumain.cpp to mainmenu.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2017 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Deals with the running of the main menu and its generic sub-menus.
 *
 */


#include "menu.h"
#include "plasma.h"

#include "game/game.h"
#include "io/controls.h"
#include "io/gfx/video.h"
#include "io/gfx/font.h"
#include "io/sound.h"
#include "jj1/scene/jj1scene.h"
#include "loop.h"
#include "util.h"
#include "io/log.h"
#include "logo.h"
#include "version.h"

#include <miniz.h>
#include <time.h>


/**
 * Create the main menu.
 */
MainMenu::MainMenu () {

	File *file;
	time_t currentTime;

	// Load the OpenJazz logo

	unsigned char* pixels = new unsigned char[oj_logo.size];

	// The decompressor's tables (about 11 KB) are too big for small stacks
	tinfl_decompressor* decomp = tinfl_decompressor_alloc();
	size_t res = TINFL_DECOMPRESS_MEM_TO_MEM_FAILED;
	if (decomp) {
		size_t inSize = oj_logo.compressed_size;
		size_t outSize = oj_logo.size;
		tinfl_init(decomp);
		if (tinfl_decompress(decomp, oj_logo.data, &inSize, pixels, pixels, &outSize,
			TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF|TINFL_FLAG_PARSE_ZLIB_HEADER) == TINFL_STATUS_DONE)
			res = outSize;
		tinfl_decompressor_free(decomp);
	}
	if (res == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || res != oj_logo.size) {

		LOG_WARN("Could not uncompress logo (expected: %d, actual: %zu).", oj_logo.size, res);
		logo = nullptr;

	} else {

		logo = video.createSurface(pixels, oj_logo.width, oj_logo.height);

	}
	delete[] pixels;

	// Load the menu graphics

	try {

		file = new File("MENU.000", PATH_TYPE_GAME);

	} catch (int e) {

		video.destroySurface(logo);

		throw;

	}

	// only available in Holiday Hare 94/95
	if (file->getSize() > 200000) {

		time(&currentTime);

		// In December, load the Christmas menu graphics
		if (localtime(&currentTime)->tm_mon == 11) {

			LOG_TRACE("Loading christmas menu theme.");

			file->skipRLE();
			file->skipRLE();
			file->skipRLE();

			file->loadPalette(palette);
			background = file->loadSurface(SW, SH);
			highlight = file->loadSurface(SW, SH);

		} else {

			file->loadPalette(palette);
			background = file->loadSurface(SW, SH);
			highlight = file->loadSurface(SW, SH);

			file->skipRLE();
			file->skipRLE();
			file->skipRLE();

		}

	} else {

		file->loadPalette(palette);
		background = file->loadSurface(SW, SH);
		highlight = file->loadSurface(SW, SH);

	}

	video.enableColorKey(background, 0);
	video.enableColorKey(highlight, 0);
	if (logo) video.enableColorKey(logo, 28);

	gameMenu = new GameMenu(file);

	delete file;

}


/**
 * Delete the main menu.
 */
MainMenu::~MainMenu () {

	video.destroySurface(background);
	video.destroySurface(highlight);
	video.destroySurface(logo);

	delete gameMenu;

}


/**
 * Process a main menu selection.
 *
 * @param option Chosen menu option
 *
 * @return Error code
 */
int MainMenu::select (int option) {

	JJ1Scene *scene;
	SetupMenu setupMenu;

	playConfirmSound();

	switch (option) {

		case 0: // New game

			if (gameMenu->newGame() == E_QUIT) return E_QUIT;

			break;

		case 1: // Load game

			if (gameMenu->loadGame() == E_QUIT) return E_QUIT;

			break;

		case 2: // Instructions

			try {

				scene = new JJ1Scene("INSTRUCT.0SC");

			} catch (int e) {

				if (message("COULD NOT LOAD INSTRUCTIONS") == E_QUIT) return E_QUIT;

				break;

			}

			if (scene->play() == E_QUIT) {

				delete scene;

				return E_QUIT;

			}

			delete scene;

			break;

		case 3: // Setup options

			if (setupMenu.run() == E_QUIT) return E_QUIT;

			break;

		case 4: // Order info

			try {

				scene = new JJ1Scene("ORDER.0SC");

			} catch (int e) {

				if (message("COULD NOT LOAD ORDER INFO") == E_QUIT) return E_QUIT;

				break;

			}

			if (scene->play() == E_QUIT) {

				delete scene;

				return E_QUIT;

			}

			delete scene;

			break;

		case 5: // Exit

			return E_RETURN;

	}

	// Restore the main menu palette
	video.setPalette(palette);

	return E_NONE;

}


/**
 * Run the main menu.
 *
 * @return Error code
 */
int MainMenu::run () {

	SDL_Rect options[6] = {
		{92, 35, 136, 22},
		{92, 57, 140, 22},
		{88, 83, 144, 22},
		{86, 109, 150, 23},
		{82, 137, 156, 26},
		{78, 166, 166, 29}};
	int macroType[4];
	Plasma plasma;
	SDL_Rect dst;
	int option, macro, x, y, ret;
	unsigned int idleTime;

	option = 0;

	video.setPalette(palette);

	playMusic("MENUSNG.PSM");


	// Demo timeout
	idleTime = globalTicks + T_DEMO;

	// Check for demo macros
	char *fileName = createString("MACRO.1");
	for (macro = 0; macro < 4; macro++) {
		macroType[macro] = -1;

		if (File::exists(fileName, PATH_TYPE_GAME)) {
			FilePtr file = std::make_unique<File>(fileName, PATH_TYPE_GAME);
			macroType[macro] = file->loadChar();
		}

		fileName[6]++;
	}
	delete[] fileName;

	macro = 3;

	while (true) {

		if (loop(NORMAL_LOOP) == E_QUIT) return E_QUIT;

		if (controls.release(C_ESCAPE)) {
			option = 5;

			// New demo timeout
			idleTime = globalTicks + T_DEMO;
		}

		if (controls.release(C_UP)) {
			option = (option + 5) % 6;

			// New demo timeout
			idleTime = globalTicks + T_DEMO;
		}

		if (controls.release(C_DOWN)) {
			option = (option + 1) % 6;

			// New demo timeout
			idleTime = globalTicks + T_DEMO;
		}

		if (controls.release(C_ENTER)) {
			ret = select(option);

			if (ret < 0) return ret;

			// New demo timeout
			idleTime = globalTicks + T_DEMO;
		}

		if (controls.getCursor(x, y)) {

			int count;

			x -= (canvasW - SW) >> 1;
			y -= (canvasH - SH) >> 1;

			for (count = 0; count < 6; count++) {

				if ((x >= options[count].x) &&
					(x < options[count].x + options[count].w) &&
					(y >= options[count].y) &&
					(y < options[count].y + options[count].h)) {

					option = count;

					if (controls.wasCursorReleased()) {

						ret = select(option);

						if (ret < 0) return ret;

					}

					// New demo timeout
					idleTime = globalTicks + T_DEMO;

					break;

				}
			}

		}


		if (idleTime <= globalTicks) {

			Game* game = NULL;

			try {

				game = new LocalGame("", difficultyType::Easy);

			} catch (int e) {

				// Do nothing

			}

			if (game) {

				// Load the macro

				x = macro;
				macro = (macro + 1) & 3;

				while ((macroType[macro] != 0xFF) && (macro != x))
					macro = (macro + 1) & 3;

				if (macro != x) {

					char *fileName = createString("MACRO.1");
					fileName[6] += macro;

					if (game->playLevel(fileName) == E_QUIT) {

						delete[] fileName;
						delete game;

						return E_QUIT;

					}
					delete[] fileName;

				}


				delete game;

				playMusic("MENUSNG.PSM");

				// Restore the main menu palette
				video.setPalette(palette);

			}

			idleTime = globalTicks + T_DEMO;

		}

		SDL_Delay(T_MENU_FRAME);


		//as long as we're drawing plasma, we don't need to clear the screen.
		//video.clearScreen(28);

		// The background goes over the plasma, logo and version string. It is
		// drawn with the plasma, and again over the logo and the text.
		int backgroundX = (canvasW - SW) >> 1;
		int backgroundY = (canvasH - SH) >> 1;
		plasma.draw(background, backgroundX, backgroundY);

		// draw logo and version string

		SDL_Rect logoRect = {0, 0, 0, 0};
		if (logo) {
			dst.x = (canvasW >> 2) - 72;
			dst.y = canvasH - (canvasH >> 2);
			SDL_BlitSurface(logo, NULL, canvas, &dst);
			logoRect = dst;
		}

		panelBigFont->mapPalette(0, MAX_PALETTE_COLORS, 8, 8);
		if(canvasW > SW)
			panelBigFont->showString("OpenJazz", 16, canvasH, alignX::Left, alignY::Bottom);
		panelBigFont->showString(oj_version, canvasW - 16, canvasH, alignX::Right, alignY::Bottom);
		panelBigFont->restorePalette();

		int textHeight = panelBigFont->getHeight() * 2;
		SDL_Rect textRect = {0, static_cast<decltype(textRect.y)>(canvasH - textHeight),
			static_cast<decltype(textRect.w)>(canvasW), static_cast<decltype(textRect.h)>(textHeight)};

		for (SDL_Rect rect: {logoRect, textRect}) {
			// From canvas to background coordinates
			rect.x -= backgroundX;
			rect.y -= backgroundY;
			dst.x = backgroundX + rect.x;
			dst.y = backgroundY + rect.y;
			SDL_BlitSurface(background, &rect, canvas, &dst);
		}

		dst.x = ((canvasW - SW) >> 1) + options[option].x;
		dst.y = ((canvasH - SH) >> 1) + options[option].y;
		SDL_BlitSurface(highlight, options + option, canvas, &dst);

	}

	return E_NONE;

}


/**
 * Skip the main menu. Directly loads game
 *
 * @return Error code
 */
int MainMenu::skip (int levelNum, int worldNum) {

	if (gameMenu->loadGame(levelNum, worldNum) == E_QUIT) return E_QUIT;

	return E_RETURN;

}
