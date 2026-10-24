
/**
 *
 * @file jj1levelframe.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created level.c
 * - 3rd February 2009: Renamed level.c to level.cpp
 * - 19th July 2009: Created levelframe.cpp from parts of level.cpp
 * - 30th March 2010: Created baselevel.cpp from parts of level.cpp and
 *                  levelframe.cpp
 * - 29th June 2010: Created jj2levelframe.cpp from parts of levelframe.cpp
 * - 1st August 2012: Renamed levelframe.cpp to jj1levelframe.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2013 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Provides the once-per-frame functions for levels.
 *
 */


#include "jj1bullet.h"
#include "event/jj1event.h"
#include "event/jj1guardians.h"
#include "jj1level.h"
#include "jj1levelplayer.h"

#include "game/game.h"
#include "game/gamemode.h"
#include "io/controls.h"
#include "io/gfx/font.h"
#include "io/gfx/video.h"
#ifdef OJ_TILED_LEVEL
	#include "platforms/gba/level_gba.h"
#endif
#include "util.h"

#include <algorithm>


/**
 * Level iteration.
 *
 * @return Error code
 */
int JJ1Level::step () {

	JJ1Event *event;
	int viewH = canvasH;
	int x, y;

	if(setup.hudStyle == hudType::Classic) {
		// Can we see below the panel?
		if (canvasW > SW) viewH = canvasH;
		else viewH = canvasH - 33;
	} else if(setup.hudStyle == hudType::FPS) {
		// Leave space for panel in view height
		viewH = canvasH - 33;
	}

#if OJ_DEBUG
	// Apply cheats
	localPlayer->getJJ1LevelPlayer()->applyCheats();
#endif

	// Search for active events, within the level
	int firstX = std::max(FTOT(viewX) - 5, 0);
	int lastX = std::min(ITOT(FTOI(viewX) + canvasW) + 5, LW);
	int firstY = std::max(FTOT(viewY) - 5, 0);
	int lastY = std::min(ITOT(FTOI(viewY) + viewH) + 5, LH);
	auto difficulty = +getDifficulty();

	for (y = firstY; y < lastY; y++) {

		for (x = firstX; x < lastX; x++) {

			int gridEvent = grid[x][y].event();

			if (gridEvent && (gridEvent < 121) &&
				(+eventSet[gridEvent].difficulty <= difficulty)) {

				event = events;

				while (event) {

					// If the event has been found, stop searching
					if (event->isFrom(x, y)) break;

					event = event->getNext();

				}

				// If the event wasn't found, create it
				if (!event) {

					switch (getEvent(x, y)->movement) {

						case 28:

							events = new JJ1Bridge(x, y);

							break;

						case 41:

							events = new MedGuardian(x, y);

							break;

						case 60:

							events = new DeckGuardian(x, y);

							break;

						default:

							events = new JJ1StandardEvent(eventSet + grid[x][y].event(), x, y, TTOF(x), TTOF(y + 1));

							break;

					}

				}

			}

		}

	}


	// Process bullets
	if (bullets) bullets = bullets->step(ticks);

	// Determine the players' trajectories
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->control(ticks);

	// Process active events
	if (events) events = events->step(ticks);

	// Apply as much of those trajectories as possible, without going into the
	// scenery
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->move(ticks);


	// Check if time has run out
	if (ticks > endTime) {

		if (multiplayer) {

			game->getMode()->outOfTime();

		} else {

			if ((getDifficulty() == difficultyType::Hard ||
				getDifficulty() == difficultyType::Turbo)
				&& (stage == LS_NORMAL))
				localPlayer->getJJ1LevelPlayer()->kill(nullptr, endTime);

		}

	}


	// Handle change in ammo selection

	x = localPlayer->getAmmoType() + 1;

	if (x != ammoType) {

		// Change the ammo type display on the panel
		ammoType = x;
		ammoOffset = ITOF(26);

	}

	if (ammoOffset > 0) {

		// Descending
		ammoOffset -= F1;

		// Avoid an offset of 0, which prevents changes
		if (ammoOffset == 0) ammoOffset = -1;

	}


	// Handle change in water level
	if (waterLevel < waterLevelTarget) waterLevelSpeed += 3200;
	else waterLevelSpeed -= 3200;
	if (waterLevelSpeed > 80000) waterLevelSpeed = 80000;
	if (waterLevelSpeed < -80000) waterLevelSpeed = -80000;

	waterLevel += waterLevelSpeed >> 6;


	// Handle player reactions
	for (x = 0; x < nPlayers; x++) {

		if (players[x].getJJ1LevelPlayer()->reacted(ticks) == PR_KILLED) {

			players[x].clearAmmo();

			if (!multiplayer) return LOST;

			game->resetPlayer(players + x);

		}

	}


	return E_NONE;

}



/**
 * Draw the level.
 */
void JJ1Level::draw () {

#ifndef OJ_TILED_LEVEL
	GridElement *ge;
#endif
	SDL_Rect src, dst;
	int viewH = canvasH;
	int vX, vY;
	int x, y;
	unsigned int change;


	// Calculate change since last step
	change = getTimeChange();


	// Calculate viewport
	if (game && (stage == LS_END)) game->view(paused? 0: ((ticks - prevTicks) * 160));
	else localPlayer->getJJ1LevelPlayer()->view(ticks, paused? 0: (ticks - prevTicks), change);

	if(setup.hudStyle == hudType::Classic) {
		// Can we see below the panel?
		if (canvasW > SW) viewH = canvasH;
		else viewH = canvasH - 33;
	} else if(setup.hudStyle == hudType::FPS) {
		// Leave space for panel in view height
		viewH = canvasH - 33;
	}

#ifdef OJ_TILED_LEVEL
	viewH = canvasH - gbaLevel::HUD_HEIGHT;
#endif

	// Ensure the new viewport is within the level
	if (FTOI(viewX) + canvasW >= TTOI(LW)) viewX = ITOF(TTOI(LW) - canvasW);
	if (viewX < 0) viewX = 0;
	if (FTOI(viewY) + viewH >= TTOI(LH)) viewY = ITOF(TTOI(LH) - viewH);
	if (viewY < 0) viewY = 0;

	// Use the viewport
	dst.x = 0;
	dst.y = 0;
	vX = FTOI(viewX);
	vY = FTOI(viewY);
	dst.w = canvasW;
	dst.h = viewH;
	video.setClipRect(canvas, &dst);


	// Set tile drawing dimensions
	src.w = TTOI(1);
	src.h = TTOI(1);
	src.x = 0;


#ifdef OJ_TILED_LEVEL
	// The hardware draws the tiles; tell it which go where
	gbaLevel::beginFrame(vX, vY);

	if (sky) {

		gbaLevel::setSky(156, ((canvasH - 34) / 100) + 1);

		// The sun / moon / etc. is a sprite behind the background tiles
		if (skyOrb) {

			canvas = gbaLevel::spriteLayer();
			gbaLevel::setSpritePriority(3);
			dst.x = ((canvasW * 4) / 5) - (vX & 3);
			dst.y = ((canvasH - 33) * 3) / 25;
			src.y = TTOI(skyOrb + (vX & 3));
			SDL_BlitSurface(tileSet, &src, canvas, &dst);
			gbaLevel::setSpritePriority(2);

		}

	} else gbaLevel::setSky(127, 0);

	setTiledCells(vX, vY);

	canvas = gbaLevel::spriteLayer();
#else
	// If there is a sky, draw it
	if (sky) {

		// Background scale
		int bgScale;
		if (canvasW > SW) bgScale = ((canvasH - 1) / 100) + 1;
		else bgScale = ((canvasH - 34) / 100) + 1;

		for (y = 0; y < viewH; y += bgScale)
			video.drawRect(0, y, canvasW, bgScale, 156 + (y / bgScale));


		// Show sun / moon / etc.
		if (skyOrb) {

			dst.x = ((canvasW * 4) / 5) - (vX & 3);
			dst.y = ((canvasH - 33) * 3) / 25;
			src.y = TTOI(skyOrb + (vX & 3));
			SDL_BlitSurface(tileSet, &src, canvas, &dst);

		}

	} else {

		// If there is no sky, draw a blank background
		// This is only very occasionally actually visible
		video.clearScreen(127);

	}



	// Show background tiles

	for (y = 0; y <= ITOT(viewH - 1) + 1; y++) {

		for (x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			if ((x + ITOT(vX) >= 256) || (y + ITOT(vY) >= 64)) {

				video.drawRect(TTOI(x) - (vX & 31), TTOI(y) - (vY & 31), 32, 32, LEVEL_BLACK);

				continue;

			}

			// Get the grid element from the given coordinates
			ge = &grid[x + ITOT(vX)][y + ITOT(vY)];

			// If this tile uses a black background, draw it
			if (ge->bg())
				video.drawRect(TTOI(x) - (vX & 31), TTOI(y) - (vY & 31), 32, 32, LEVEL_BLACK);


			// If this is not a foreground tile, draw it
			if ((ge->event() != 123) && (ge->event() != 124) && (ge->event() != 125) &&
				(eventSet[ge->event()].movement != 37) &&
				(eventSet[ge->event()].movement != 38)) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				src.y = TTOI(ge->tile);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_BGTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 55, false);
#endif
			}

		}

	}


#endif


	// Show active events
	if (events) events->draw(ticks, change);


	// Show the players
	for (x = 0; x < nPlayers; x++) players[x].getJJ1LevelPlayer()->draw(ticks, change);


	// Show bullets
	if (bullets) bullets->draw(change);


#ifdef OJ_TILED_LEVEL
	canvas = gbaLevel::overlay();
#else
	// Show foreground tiles

	for (y = 0; y <= ITOT(viewH - 1) + 1; y++) {

		for (x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			if ((x + ITOT(vX) >= 256) || (y + ITOT(vY) >= 64)) continue;

			// Get the grid element from the given coordinates
			ge = &grid[x + ITOT(vX)][y + ITOT(vY)];

			// If this is an "animated" foreground tile, draw it
			if (ge->event() == 123) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				if (ticks & 64) src.y = TTOI(eventSet[ge->event()].multiB);
				else src.y = TTOI(eventSet[ge->event()].multiA);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_ANIMTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 44, false);
#endif
			}

			// If this is a foreground tile, draw it
			if ((ge->event() == 124) || (ge->event() == 125) ||
				(eventSet[ge->event()].movement == 37) ||
				(eventSet[ge->event()].movement == 38)) {

				dst.x = TTOI(x) - (vX & 31);
				dst.y = TTOI(y) - (vY & 31);
				src.y = TTOI(ge->tile);
				SDL_BlitSurface(tileSet, &src, canvas, &dst);

#if OJ_DEBUG
				if(debug::showFlags & DS_TILEMASK)
					SDL_BlitSurface(maskedTileset, &src, canvas, &dst);
				if(debug::showFlags & DS_FGTILES)
					video.drawRect(dst.x, dst.y, TTOI(1), TTOI(1), 33, false);
#endif
			}

		}

	}
#endif

	// FIXME: Temporary lines showing the water level
	video.drawRect(0, FTOI(waterLevel - viewY), canvasW, 2, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 3, canvasW, 1, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 6, canvasW, 1, 24);
	video.drawRect(0, FTOI(waterLevel - viewY) + 10, canvasW, 1, 24);

	// Show active guardian's energy bar
	if (events) events->drawEnergy(ticks);


	// If this is a competitive game, draw the score
	if (multiplayer) game->getMode()->drawScore(font);


	// Move the health bar towards the player's energy
	x = localPlayer->getJJ1LevelPlayer()->getEnergy();
	y = (ticks - prevTicks) * 40;

	if (FTOI(energyBar) < x) {
		// increase

		if (ITOF(x) - energyBar < y) energyBar = ITOF(x);
		else energyBar += y;

	} else if (FTOI(energyBar) > x) {
		// decrease

		if (energyBar - ITOF(x) < y) energyBar = ITOF(x);
		else energyBar -= y;

	}


#ifdef OJ_TILED_LEVEL
	// The panel stays in VRAM, so only the parts that change are drawn again
	int energy = localPlayer->getJJ1LevelPlayer()->getEnergy();
	const gbaLevel::HudField hudFields[] = {
		{localPlayer->getScore(), 16, 88},
		{(endTime > ticks)? static_cast<int>(endTime - ticks) / 100: 0, 100, 152},
		{localPlayer->getLives(), 104, 128},
		{worldNum, 168, 200},
		{levelNum, 168, 200},
		{localPlayer->getAmmoType(), 212, 256},
		{localPlayer->getAmmo(), 212, 256},
		{ammoType, 244, 320},
		{ammoOffset, 244, 320},
		{FTOI(energyBar), 16, 88},
		{energy, 16, 88},
		{(energy <= 20)? static_cast<int>(((ticks / 75) * 4) & 15): 0, 16, 88}
	};
	if (!gbaLevel::beginHud(hudFields, sizeof(hudFields) / sizeof(hudFields[0]))) return;
#endif

	// Show panel

	int offsetX = 0;
	bool isWide = (canvasW != SW);

	if(setup.hudStyle == hudType::FPS && isWide) {
		// center panel
		offsetX = (canvasW >> 1) - (SW >> 1);
	}

	video.setClipRect(canvas, nullptr);

	if (ammoOffset != 0) {

		if (ammoOffset < 0) {

			// Finished descending
			ammoOffset = 0;

		}

		src.x = 0;
		src.y = FTOI(ammoOffset);
		src.w = 64;
		src.h = 26 - src.y;
		dst.x = 248;
		dst.y = 3;
		SDL_BlitSurface(panelAmmo[ammoType], &src, panel, &dst);

	}

	// always classic HUD if there is not enough space
	if(setup.hudStyle == hudType::Classic ||
		(setup.hudStyle == hudType::FPS && !isWide)) {

		dst.x = 0;
		dst.y = canvasH - 33;
		SDL_BlitSurface(panel, nullptr, canvas, &dst);

		// Fill the one missing pixel row at the bottom black
		video.drawRect(0, canvasH - 1, SW, 1, LEVEL_BLACK);
	} else if (setup.hudStyle == hudType::FPS) {
		// quake-style HUD

		dst.y = canvasH - 33;

		for (x = 0; x <= ITOT(offsetX); x++) {
			// draw left border
			dst.x = TTOI(x);
			SDL_BlitSurface(panelBG[0], nullptr, canvas, &dst);

			// draw right border
			dst.x = canvasW - TTOI(x + 1);
			SDL_BlitSurface(panelBG[1], nullptr, canvas, &dst);
		}

		// cut the panel borders and re-center
		src.x = 1; // left: 1 white pixel
		src.y = 0;
		src.w = SW - 8; // right: 3 black pixels + 5 pixels around screws
		src.h = TTOI(1);
		dst.x = offsetX + 5;
		SDL_BlitSurface(panel, &src, canvas, &dst);

		// Fill the one missing pixel row at the bottom black
		video.drawRect(0, canvasH - 1, canvasW, 1, LEVEL_BLACK);

		offsetX += 4; // move everything inside right
	}


	// Show panel data

	// Show score
	panelSmallFont->showNumber(localPlayer->getScore(), offsetX + 84, canvasH - 27);

	// Show time remaining
	if (endTime > ticks) x = endTime - ticks;
	else x = 0;
	y = x / (60 * 1000);
	panelSmallFont->showNumber(y, offsetX + 116, canvasH - 27);
	x -= (y * 60 * 1000);
	y = x / 1000;
	panelSmallFont->showNumber(y, offsetX + 136, canvasH - 27);
	x -= (y * 1000);
	y = x / 100;
	panelSmallFont->showNumber(y, offsetX + 148, canvasH - 27);

	// Show lives
	panelSmallFont->showNumber(localPlayer->getLives(), offsetX + 124, canvasH - 13);

	// Show planet number

	if (worldNum <= 41) // Main game levels
		panelSmallFont->showNumber((worldNum % 3) + 1, offsetX + 184, canvasH - 13);
	else if ((worldNum >= 50) && (worldNum <= 52)) // Christmas levels
		panelSmallFont->showNumber(worldNum - 49, offsetX + 184, canvasH - 13);
	else panelSmallFont->showNumber(worldNum, offsetX + 184, canvasH - 13);

	// Show level number
	panelSmallFont->showNumber(levelNum + 1, offsetX + 196, canvasH - 13);



	// Show ammo
	bool unlimitedAmmo = localPlayer->getAmmoType() == -1; // blaster
#if OJ_DEBUG
	// force unlimited ammo
	if(debug::cheatFlags & DC_AMMO)
		unlimitedAmmo = true;
#endif
	if (unlimitedAmmo) {

		// Draw "infinity" symbol
		panelSmallFont->showString(":", offsetX + 225, canvasH - 13);
		panelSmallFont->showString(";", offsetX + 233, canvasH - 13);

	} else {

		x = localPlayer->getAmmo();

		// Trailing 0s
		if (x < 100) {

			panelSmallFont->showNumber(0, offsetX + 229, canvasH - 13);
			if (x < 10) panelSmallFont->showNumber(0, offsetX + 237, canvasH - 13);

		}

		panelSmallFont->showNumber(x > 999? 999: x, offsetX + 245, canvasH - 13);

	}


	// Draw the health bar

	dst.x = 20;
	x = localPlayer->getJJ1LevelPlayer()->getEnergy();

	if (energyBar > F1) {

		dst.w = FTOI(energyBar) - 1;

		// Choose energy bar colour
		int color;
		if (x <= 20) color = 32 + (((ticks / 75) * 4) & 15); // flash
		else if (x > 51) color = 24; // blue only before first hit
		else if (x > 38) color = 17; // green
		else if (x > 25) color = 80; // pink
		else if (x > 20) color = 32; // orange is only seen in easy

		// Draw energy bar
		video.drawRect(offsetX + dst.x, canvasH - 13, dst.w, 7, color);

		dst.x += dst.w;
		dst.w = 64 - dst.w;

	} else dst.w = 64;


	// Fill in remaining energy bar space with black
	video.drawRect(offsetX + dst.x, canvasH - 13, dst.w, 7, LEVEL_BLACK);

#ifdef OJ_TILED_LEVEL
	gbaLevel::endHud();

#endif
}


#ifdef OJ_TILED_LEVEL
/**
 * Tell the tile hardware which tiles each level cell on screen shows.
 *
 * @param vX Level pixel at the screen's left edge
 * @param vY Level pixel at the screen's top edge
 */
OJ_FAST_CODE void JJ1Level::setTiledCells (int vX, int vY) {

	for (int y = 0; y <= ITOT(canvasH - 1) + 1; y++) {

		for (int x = 0; x <= ITOT(canvasW - 1) + 1; x++) {

			int cellX = x + ITOT(vX);
			int cellY = y + ITOT(vY);

			if ((cellX >= LW) || (cellY >= LH)) {

				gbaLevel::setCell(cellX, cellY, -1, true, -1);
				continue;

			}

			GridElement* ge = &grid[cellX][cellY];

			// The same choices as the drawing loops in draw()
			int event = ge->event();
			bool foreground = (event == 124) || (event == 125) ||
				(eventSet[event].movement == 37) || (eventSet[event].movement == 38);
			int fgTile = -1;
			if (event == 123) fgTile = (ticks & 64)? eventSet[event].multiB: eventSet[event].multiA;
			if (foreground) fgTile = ge->tile;

			gbaLevel::setCell(cellX, cellY, (foreground || (event == 123))? -1: ge->tile, ge->bg(), fgTile);

		}

	}

}
#endif
