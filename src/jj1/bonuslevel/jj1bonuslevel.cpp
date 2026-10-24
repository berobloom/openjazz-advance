
/**
 *
 * @file jj1bonuslevel.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created bonus.c
 * - 3rd February 2009: Renamed bonus.c to bonus.cpp
 * - 1st August 2012: Renamed bonus.cpp to jj1bonuslevel.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2017 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Deals with the loading, running and freeing of bonus levels.
 *
 */


#include "jj1bonuslevelplayer.h"
#include "jj1bonuslevel.h"

#include "game/game.h"
#include "game/gamemode.h"
#include "io/controls.h"
#include "io/file.h"
#include "io/gfx/font.h"
#include "io/gfx/paletteeffects.h"
#include "io/gfx/sprite.h"
#include "io/gfx/video.h"
#include "io/sound.h"
#include "io/log.h"
#include "util.h"

#if OJ_PACK
	#include "io/pack.h"
#endif
#ifdef OJ_SCALED_CANVAS
	#include "platforms/gba/blit_gba.h"
#endif

#include <algorithm>
#include <memory>
#include <string.h>


/**
 * Load sprites.
 *
 * @return Error code
 */
int JJ1BonusLevel::loadSprites () {

	FilePtr file;
	unsigned char* pixels;

	try {

		file = std::make_unique<File>("BONUS.000", PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}

	file->seek(2, true);

	sprites = file->loadShort(256);
	spriteSet = new Sprite[sprites];

	for (int i = 0; i < sprites; i++) {

		// Load dimensions
		int width = file->loadShort(SW);
		int height = file->loadShort(SH);

		int pixelsLength = file->loadShort();
		int maskLength = file->loadShort();

		// Sprites can be either masked or not masked.
		if (pixelsLength != 0xFFFF) {

			// Masked
			width <<= 2;

			int pos = file->tell() + (pixelsLength << 2) + maskLength;

			// Read scrambled, masked pixel data
			pixels = file->loadPixels(width * height, 0);
			spriteSet[i].setPixels(pixels, width, height, 0);

			delete[] pixels;

			file->seek(pos, true);

		} else if (width) {

			// Not masked

			// Read pixel data
			pixels = file->loadBlock(width * height);
			spriteSet[i].setPixels(pixels, width, height, 0);

			delete[] pixels;

		} else {

			// Zero-length sprite

			// Create blank sprite
			spriteSet[i].clearPixels();

		}

	}

	return E_NONE;

}


/**
 * Load the tileset.
 *
 * @param fileName Name of the file containing the tileset
 *
 * @return Error code
 */
int JJ1BonusLevel::loadTiles (char *fileName) {

	FilePtr file;
	unsigned char *pixels;
	unsigned char *sorted;
	int count, x, y;

	direction = 0;

	try {

		file = std::make_unique<File>(fileName, PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}

	// Load background
	pixels = file->loadRLE(832 * 20);
	sorted = new unsigned char[512 * 20];

	for (count = 0; count < 20; count++) memcpy(sorted + (count * 512), pixels + (count * 832), 512);

	background = video.createSurface(sorted, 512, 20);

	delete[] sorted;
	delete[] pixels;

	// Load palette
	file->loadPalette(palette);

	// Load tile graphics
	pixels = file->loadRLE(1024 * 60);
	tileSet = video.createSurface(pixels, 32, 32 * 60);

	// Create mask
	for (count = 0; count < 60; count++) {

		memset(mask[count], 0, 64);

		for (y = 0; y < 32; y++) {

			for (x = 0; x < 32; x++) {

				if ((pixels[(count << 10) + (y << 5) + x] & 240) == 192)
					mask[count][((y << 1) & 56) + ((x >> 2) & 7)] = 1;

			}

		}

	}

	delete[] pixels;

	return E_NONE;

}


/**
 * Create a JJ1 bonus level.
 *
 * @param owner The current game
 * @param fileName Name of the file containing the level data.
 * @param multi Whether or not the level will be multi-player
 */
namespace {

	/**
	 * Make a layer of the grid writable, by copying it if it is the game data
	 * pack's read-only copy.
	 *
	 * @param layer The layer
	 */
	void makeWritable (unsigned char*& layer) {

#if OJ_PACK
		if (packContains(layer)) {
			unsigned char* copy = new unsigned char[BLW * BLH];
			memcpy(copy, layer, BLW * BLH);
			layer = copy;
		}
#else
		OJ_UNUSED(layer);
#endif

	}

}


JJ1BonusLevel::JJ1BonusLevel (Game* owner, char * fileName, bool multi) : Level(owner) {

	Anim* pAnims[BPANIMS];
	unsigned short int soundRates[BSOUNDS];
	FilePtr file;
	unsigned char *buffer;
	char *string, *fileString;
	int count, x, y;

	try {

		font = new Font(true);

	} catch (int e) {

		throw;

	}
	#if DEBUG_FONTS
	font->saveAtlasAsBMP("bonusfont.bmp");
	#endif

	try {

		file = std::make_unique<File>(fileName, PATH_TYPE_GAME);

	} catch (int e) {

		delete font;

		throw;

	}

	// Load sprites
	count = loadSprites();

	if (count < 0) {

		delete font;

		throw count;

	}

	// Skip Editor data files
	file->seek(10 * 9, true);

	// Load tileset
	string = file->loadTerminatedString(8);
	fileString = createFileName(string, 0);
	x = loadTiles(fileString);
	delete[] string;
	delete[] fileString;

	if (x != E_NONE) throw x;

	// Skip Editor tileset files
	file->seek(9 + 13);

	// Load music
	fileString = file->loadTerminatedString(12);
	playMusic(fileString);
	delete[] fileString;


	// Load animations
	buffer = file->loadBlock(BANIMS << 6);

	// Create animation set based on that data
	for (count = 0; count < BANIMS; count++) {

		animSet[count].setData(buffer[(count << 6) + 6],
			buffer[count << 6], buffer[(count << 6) + 1],
			buffer[(count << 6) + 3], buffer[(count << 6) + 4],
			buffer[(count << 6) + 2], buffer[(count << 6) + 5]);

		for (y = 0; y < buffer[(count << 6) + 6]; y++) {

			// Get frame
			x = buffer[(count << 6) + 7 + y];
			if (x > sprites){
				LOG_DEBUG("Clipping Sprite index %d > %d", x, sprites);
				x = sprites;
			}

			// Assign sprite and vertical offset
			animSet[count].setFrame(y, true);
			animSet[count].setFrameData(spriteSet + x,
				buffer[(count << 6) + 26 + y], buffer[(count << 6) + 45 + y]);

		}

	}

	delete[] buffer;


#if DEBUG_LOAD
	// Show animation names
	for (count = 0; count < BANIMS; count++) {
		char *tmpName = file->loadTerminatedString(15);
		if(strlen(tmpName)) {
			LOG_MAX("Animation id %d is named \"%s\"", count, tmpName);
		}
		delete[] tmpName;
	}
#else
	// Skip animation names
	file->seek(BANIMS * 16);
#endif


	// Load tiles
	tiles = file->loadRLE(BLW * BLH);

	if (std::any_of(tiles, tiles + BLW * BLH, [](unsigned char tile) { return tile > 59; })) {

		makeWritable(tiles);

		for (count = 0; count < BLW * BLH; count++) {
			if (tiles[count] > 59) {
				LOG_DEBUG("Clipping Tile index %d > %d", tiles[count], 59);
				tiles[count] = 59;
			}
		}

	}


	// Load event properties
	buffer = file->loadRLE(BEVENTS * 16);
	for (count = 0; count < BEVENTS; count++) {
		eventSet[count].anim = buffer[(count * BEVENTS) + 5];
		eventSet[count].type = buffer[(count * BEVENTS) + 7];
		eventSet[count].passable = buffer[(count * BEVENTS) + 8];
		eventSet[count].used = buffer[(count * BEVENTS) + 9];

		auto se = static_cast<SE::Type>(buffer[(count * BEVENTS) + 13]);
		if (!isValidSoundIndex(se)) {
			eventSet[count].sound = SE::NONE;
			LOG_WARN("Event %d has invalid sound effect %d.", count, se);
		} else {
			eventSet[count].sound = se;
		}

		// This is a hack to make "exit signs" work in OJ
		if(eventSet[count].type == 7)
			eventSet[count].passable = 1;

#if DEBUG_LOAD
		//hexDump(nullptr, buffer + count * BEVENTS, 16);
		LOG_MAX("Event %d: anim=%d, type=%d, passable=%d, sound=%d",
			count, eventSet[count].anim, eventSet[count].type,
			eventSet[count].passable, eventSet[count].sound);
#endif
	}
	delete[] buffer;


	// Load event mapping
	events = file->loadRLE(BLW * BLH);


	// Load sound map
	for (count = 0; count < BSOUNDS; count++) {
		soundRates[count] = file->loadShort();
	}
	for (count = 0; count < BSOUNDS; count++) {
		char *tmpName = file->loadTerminatedString(8);
		resampleSound(count, tmpName, soundRates[count]);
		delete[] tmpName;
	}

	// Unknown marker
	if(file->loadShort() != 0xFFFF) {
		LOG_WARN("Invalid bonus level");
		throw E_FILE;
	}

	// Set the tick at which the level will end
	endTime = file->loadShort() * 1000;


	// Number of gems to collect
	items = file->loadShort();


	// The players' coordinates
	x = file->loadShort();
	y = file->loadShort();


	// Generate player's animation set references
	for (count = 0; count < BPANIMS; count++)
		pAnims[count] = animSet + count;


	createLevelPlayers(LT_JJ1BONUS, pAnims, NULL, false, x, y);


	// Palette animations

	// Spinny whirly thing
	paletteEffects = new RotatePaletteEffect(112, 16, F32, NULL);

	// Track sides
	paletteEffects = new RotatePaletteEffect(192, 16, F32, paletteEffects);

	// Bouncy things
	paletteEffects = new RotatePaletteEffect(240, 16, F32, paletteEffects);


	// Adjust panelBigFont to use bonus level palette
	panelBigFont->mapPalette(0, 32, 15, -16);


	multiplayer = multi;

	video.setTitle("Bonus Level");

}


/**
 * Delete the JJ1 bonus level.
 */
JJ1BonusLevel::~JJ1BonusLevel () {

	// Restore panelBigFont palette
	panelBigFont->restorePalette();

	video.destroySurface(tileSet);
	video.destroySurface(background);

	delete[] spriteSet;

	delete[] tiles;
	delete[] events;

	delete font;

	resampleSounds();

	video.setTitle(NULL);

}


/**
 * Determine whether or not the given point is in the event area of its tile.
 *
 * @param x X-coordinate
 * @param y Y-coordinate
 *
 * @return True if in the event area
 */
bool JJ1BonusLevel::isEvent (fixed x, fixed y) {

	return ((x & 32767) > F12) && ((x & 32767) < F20) &&
		((y & 32767) > F12) && ((y & 32767) < F20);

}


/**
 * Determine whether or not the given point is solid.
 *
 * @param x X-coordinate
 * @param y Y-coordinate
 *
 * @return Solidity
 */
bool JJ1BonusLevel::checkMask (fixed x, fixed y) {

	int gridX = FTOT(x) & 255;
	int gridY = FTOT(y) & 255;
	unsigned char event = getEvent(gridX, gridY);

	// Bounce back
	if ((eventSet[event].used) && !(eventSet[event].passable) && isEvent(x, y))
		return true;

	// Check the mask in the tile in question
	return mask[tiles[(gridY * BLW) + gridX]][((y >> 9) & 56) + ((x >> 12) & 7)];

}


/**
 * Get the event type at a grid element.
 *
 * @param gridX X-coordinate of the grid element
 * @param gridY Y-coordinate of the grid element
 *
 * @return The event type, or 0 if it was collected
 */
unsigned char JJ1BonusLevel::getEvent (int gridX, int gridY) {

	unsigned short element = (gridY * BLW) + gridX;

	if (std::binary_search(takenEvents.begin(), takenEvents.end(), element)) return 0;

	return events[element];

}


/**
 * Remove the event at a grid element, which has been collected.
 *
 * @param gridX X-coordinate of the grid element
 * @param gridY Y-coordinate of the grid element
 */
void JJ1BonusLevel::clearEvent (int gridX, int gridY) {

	unsigned short element = (gridY * BLW) + gridX;
	auto it = std::lower_bound(takenEvents.begin(), takenEvents.end(), element);

	if (it == takenEvents.end() || *it != element) takenEvents.insert(it, element);

}


/**
 * Interpret data received from client/server
 *
 * @param buffer Received data
 */
void JJ1BonusLevel::receive (unsigned char* buffer) {

	switch (buffer[1]) {

		case MT_L_PROP:

			if (buffer[2] == 2) {

				if (stage == LS_NORMAL)
					endTime += buffer[3] * 1000;

			}

			break;

		case MT_L_GRID:

			if (buffer[4] == 0) {

				makeWritable(tiles);
				tiles[(buffer[3] * BLW) + buffer[2]] = buffer[5];

			} else if (buffer[4] == 2) {

				unsigned short element = (buffer[3] * BLW) + buffer[2];
				auto it = std::lower_bound(takenEvents.begin(), takenEvents.end(), element);
				if (it != takenEvents.end() && *it == element) takenEvents.erase(it);

				makeWritable(events);
				events[element] = buffer[5];

			}

			break;

		case MT_L_STAGE:

			stage = LevelStage(buffer[2]);

			break;

	}

}


/**
 * Level iteration.
 *
 * @return Error code
 */
int JJ1BonusLevel::step () {
	// Check if time has run out
	if (ticks > endTime) {
		playSound(SE::OW);

		return LOST;
	}

	// Apply controls to local player
	for (int i = 0; i < PCONTROLS; i++)
		localPlayer->setControl(i, controls.getState(i));

	// Process players
	for (int i = 0; i < nPlayers; i++) {

		JJ1BonusLevelPlayer* bonusPlayer = players[i].getJJ1BonusLevelPlayer();

		fixed playerX = bonusPlayer->getX();
		fixed playerY = bonusPlayer->getY();

		bonusPlayer->step(ticks, 16, this);

		if ((bonusPlayer->getZ() < FH) && isEvent(playerX, playerY)) {

			int gridX = FTOT(playerX) & 255;
			int gridY = FTOT(playerY) & 255;

			unsigned char event = getEvent(gridX, gridY);

			// Play sound if available
			playSound(eventSet[event].sound);

			switch (eventSet[event].type) {

				case 1: // Extra time

					addTimer(60);
					clearEvent(gridX, gridY);

					break;

				case 2: // Gem

					bonusPlayer->addGem();
					clearEvent(gridX, gridY);

					if (bonusPlayer->getGems() >= items) {

						players[i].addLife();

						return WON;

					}

					break;

				case 5: // Hand
				case 8:

					break;

				case 7: // Exit

					return LOST;

				default:

					// Do nothing

					break;

			}

		}

	}

	direction = localPlayer->getJJ1BonusLevelPlayer()->getDirection();

	return E_NONE;

}


#ifdef OJ_SCALED_CANVAS
/**
 * How far ahead the ground is at a canvas row.
 *
 * @param y Canvas rows from the bottom
 * @param perHorizonRow 2^20 divided by the rows from the bottom to the horizon
 *
 * @return Distance
 */
static inline fixed groundDistance (int y, int perHorizonRow) {

	return DIV(ITOF(800), ITOF(92) - ((y * 84 * perHorizonRow) >> 10));

}


/**
 * Draw a row of the ground, from one level pixel every two columns.
 *
 * @param out First four columns of the row, as the canvas takes 32-bit writes
 * @param end Past the last four columns
 * @param nextRow Words from a column to the one below, to draw two rows, or 0
 * @param u Level x of the first level pixel, with 19 fractional bits
 * @param v Level y of the first level pixel, with 19 fractional bits
 * @param du Change in u to the next level pixel
 * @param dv Change in v to the next level pixel
 * @param grid Grid element tile numbers, 256 by 256
 * @param tilePixels The tile set, 32x32 tiles one below the other
 */
static OJ_FAST_CODE void drawGroundRow (unsigned int* out, unsigned int* end, int nextRow,
	unsigned int u, unsigned int v, int du, int dv, const unsigned char* grid, const unsigned char* tilePixels) {

	// Neighbouring pixels are mostly in the same grid element
	unsigned int lastElement = ~0u;
	const unsigned char* tile = tilePixels;

	for (; out < end; out++) {

		unsigned int pixels = 0;

#pragma GCC unroll 2
		for (int shift = 0; shift < 32; shift += 16) {

			unsigned int element = ((v >> 16) & 0xFF00) | (u >> 24);
			if (element != lastElement) {
				lastElement = element;
				tile = tilePixels + (grid[element] << 10);
			}

			pixels |= tile[((v >> 14) & (31 << 5)) | ((u >> 19) & 31)] << shift;
			u += du;
			v += dv;

		}

		pixels *= 0x0101;
		out[0] = pixels;
		if (nextRow) out[nextRow] = pixels;

	}

}
#endif


/**
 * Draw the ground, in perspective.
 *
 * @param playerX Player's x-coordinate
 * @param playerY Player's y-coordinate
 * @param playerSin Sine of the player's direction
 * @param playerCos Cosine of the player's direction
 */
void JJ1BonusLevel::drawGround (fixed playerX, fixed playerY, fixed playerSin, fixed playerCos) {

	if (SDL_MUSTLOCK(canvas)) SDL_LockSurface(canvas);

	// Loop over the canvas rows as stored, from canvasH - 1 up to y = 1
	int endRow = canvasRows(canvas, canvasH);
	int endColumn = canvasColumns(canvas, canvasW);

#ifdef OJ_SCALED_CANVAS
	const unsigned char* tilePixels = static_cast<unsigned char*>(tileSet->pixels);

	// To divide by these with 32-bit multiplications, as Thumb code has no long ones
	int perColumn = (1 << 16) / endColumn;
	int perHorizonRow = (1 << 20) / ((canvasH >> 1) - 16);

	int storedRow = canvasRows(canvas, canvasH - ((canvasH >> 1) - 15));
	fixed nextDistance = groundDistance(canvasH - canvasRowOf(canvas, storedRow), perHorizonRow);

	for (; storedRow < endRow; storedRow++) {

		fixed distance = nextDistance;
		if (storedRow + 1 < endRow) nextDistance = groundDistance(canvasH - canvasRowOf(canvas, storedRow + 1), perHorizonRow);

		// Near the bottom, a level pixel is more than two rows tall, so draw two rows at once
		bool doubled = (storedRow + 1 < endRow) && (distance - nextDistance < (F1 >> 1));
		if (doubled && (storedRow + 2 < endRow)) nextDistance = groundDistance(canvasH - canvasRowOf(canvas, storedRow + 2), perHorizonRow);

		fixed sideX = MUL(distance, playerCos);
		fixed sideY = MUL(distance, playerSin);
		fixed fwdX = playerX + MUL(distance - F16, playerSin) - (sideX >> 1);
		fixed fwdY = playerY - MUL(distance - F16, playerCos) - (sideY >> 1);

		/* Level pixels with 19 fractional bits, so the top 8 bits are the grid
		   element. The level wraps every 2^32 of these. */
		unsigned int u = static_cast<unsigned int>(fwdX) << 9;
		unsigned int v = static_cast<unsigned int>(fwdY) << 9;
		int du = ((sideX << 1) * perColumn) >> 8;
		int dv = ((sideY << 1) * perColumn) >> 8;

		/* Even at the horizon, each level pixel is more than two columns wide,
		   so take one in two columns, from the middle of each pair */
		u += du;
		v += dv;
		du <<= 1;
		dv <<= 1;

		unsigned int* out = reinterpret_cast<unsigned int*>(static_cast<unsigned char*>(canvas->pixels) + (canvas->pitch * storedRow));
		drawGroundRow(out, out + (endColumn >> 2), doubled? (canvas->pitch >> 2): 0, u, v, du, dv, tiles, tilePixels);

		if (doubled) storedRow++;

	}
#else
	// How far across the canvas each column is, which only depends on the column
	std::unique_ptr<fixed[]> across(new fixed[endColumn]);
	for (int column = 0; column < endColumn; column++)
		across[column] = ITOF(canvasColumnOf(canvas, column)) / canvasW;

	const unsigned char* tilePixels = static_cast<unsigned char*>(tileSet->pixels);
	int tilePitch = tileSet->pitch;

	for (int storedRow = canvasRows(canvas, canvasH - ((canvasH >> 1) - 15)); storedRow < endRow; storedRow++) {

		int y = canvasH - canvasRowOf(canvas, storedRow);

		fixed distance = DIV(ITOF(800), ITOF(92) - (ITOF(y * 84) / ((canvasH >> 1) - 16)));
		fixed sideX = MUL(distance, playerCos);
		fixed sideY = MUL(distance, playerSin);
		fixed fwdX = playerX + MUL(distance - F16, playerSin) - (sideX >> 1);
		fixed fwdY = playerY - MUL(distance - F16, playerCos) - (sideY >> 1);

		unsigned char* row = static_cast<unsigned char*>(canvas->pixels) + (canvas->pitch * storedRow);

		// Neighbouring pixels are mostly in the same grid element
		int lastElement = -1;
		const unsigned char* tile = tilePixels;

		for (int column = 0; column < endColumn; column++) {

			fixed nX = across[column];
			int levelX = FTOI(fwdX + MUL(nX, sideX));
			int levelY = FTOI(fwdY + MUL(nX, sideY));
			int element = ((ITOT(levelY) & 255) * BLW) + (ITOT(levelX) & 255);

			if (element != lastElement) {
				lastElement = element;
				tile = tilePixels + (tiles[element] << 10);
			}

			row[column] = tile[((levelY & 31) * tilePitch) + (levelX & 31)];

		}

	}
#endif

	if (SDL_MUSTLOCK(canvas)) SDL_UnlockSurface(canvas);

}


#ifdef OJ_SCALED_CANVAS
/**
 * Draw part of the sky, in one pass over the stored rows: one colour per row,
 * from 171 just above the background up to 128, which fills the rest.
 *
 * @param top First canvas row
 * @param bottom Canvas row below the last
 */
void JJ1BonusLevel::drawSky (int top, int bottom) {

	if (SDL_MUSTLOCK(canvas)) SDL_LockSurface(canvas);

	int lowest = (canvasH >> 1) - 5;
	if (bottom > lowest + 1) bottom = lowest + 1;
	int endRow = canvasRows(canvas, bottom);
	int words = canvasColumns(canvas, canvasW) >> 2;

	for (int row = canvasRows(canvas, top < 0? 0: top); row < endRow; row++) {

		int color = 171 - (lowest - canvasRowOf(canvas, row));
		unsigned int value = (color < 128? 128u: color) * 0x01010101u;

		gbaFillWords(static_cast<unsigned char*>(canvas->pixels) + (canvas->pitch * row), value, words);

	}

	if (SDL_MUSTLOCK(canvas)) SDL_UnlockSurface(canvas);

}
#endif


/**
 * Draw the level.
 */
void JJ1BonusLevel::draw () {

	JJ1BonusLevelPlayer *bonusPlayer;
	Anim *anim;
	SDL_Rect dst;
	int x, y;

	// Draw the background

	for (x = -(direction & 1023); x < canvasW; x += background->w) {

		dst.x = x;
		dst.y = (canvasH >> 1) - 4;
		SDL_BlitSurface(background, NULL, canvas, &dst);

	}

#ifdef OJ_SCALED_CANVAS
	/* The canvas keeps the sky from frame to frame, so only draw it again
	   behind the text and wherever sprites went last time */
	drawSky(0, font->getHeight());
	drawSky(std::max(skyDamage, font->getHeight()), canvasH);
	int keptSky = skyDamage;
	skyDamage = canvasH;
#else
	x = 171;

	for (y = (canvasH >> 1) - 5; (y >= 0) && (x > 128); y--)
		video.drawRect(0, y, canvasW, 1, x--);

	if (y > 0)
		video.drawRect(0, 0, canvasW, y + 1, 128);
#endif


	bonusPlayer = localPlayer->getJJ1BonusLevelPlayer();


	// Draw the ground

	fixed playerX = bonusPlayer->getX();
	fixed playerY = bonusPlayer->getY();
	fixed playerSin = fSin(direction);
	fixed playerCos = fCos(direction);

	drawGround(playerX, playerY, playerSin, playerCos);


	// Draw nearby events

	for (y = -6; y < 6; y++) {
		int evY = (((direction - FQ) & 512)? y: -y);
		fixed sY = TTOF(evY) + F16 - (playerY & 32767);

		for (x = -6; x < 6; x++) {
			int evX = ((direction & 512)? x: -x);
			fixed sX = TTOF(evX) + F16 - (playerX & 32767);

			fixed divisor = F16 + MUL(sX, playerSin) - MUL(sY, playerCos);

			if (FTOI(divisor) > 8) {
				unsigned char event = getEvent((evX + FTOT(playerX)) & 255, (evY + FTOT(playerY)) & 255);

				if (eventSet[event].type > 0) {
					anim = animSet + eventSet[event].anim;

					anim->setFrame(ticks / 75, true);
					fixed nX = DIV(MUL(sX, playerCos) + MUL(sY, playerSin), divisor);
					fixed scale = DIV(F64 * canvasW / SW, divisor);
					anim->drawScaled(ITOF(FTOI(nX * canvasW) + (canvasW >> 1)),
						ITOF(canvasH >> 1), scale);

#ifdef OJ_SCALED_CANVAS
					skyDamage = std::min(skyDamage, (canvasH >> 1) - (FTOI(anim->getHeight() * scale) >> 1));
#endif
				}
			}
		}
	}


	// Show the player
#ifdef OJ_SCALED_CANVAS
	skyDamage = std::min(skyDamage, bonusPlayer->draw(ticks));

	// The sky between the text and the sprites stays as it was on screen
	SDL_GBA_KeepRows(canvasRows(canvas, font->getHeight()),
		canvasRows(canvas, std::min(std::min(keptSky, skyDamage), (canvasH >> 1) - 4)));
#else
	bonusPlayer->draw(ticks);
#endif


	// Show gem count
	font->showString("*", 0, 0);
	font->showNumber(bonusPlayer->getGems() / 10, 50, 0);
	font->showNumber(bonusPlayer->getGems() % 10, 68, 0);
	font->showString("/", 65, 0);
	font->showNumber(items, 124, 0);


	// Show time remaining
	if (endTime > ticks) x = (endTime - ticks) / 1000;
	else x = 0;
	font->showNumber(x / 60, 250, 0);
	font->showString(":", 247, 0);
	font->showNumber((x / 10) % 6, 274, 0);
	font->showNumber(x % 10, 291, 0);

}


/**
 * Play the level.
 *
 * @return Error code
 */
int JJ1BonusLevel::play () {

	bool pmenu, pmessage;
	int option;
	unsigned int returnTime;

	tickOffset = globalTicks;
	ticks = T_STEP;
	steps = 0;
	pmessage = pmenu = false;
	option = 0;
	returnTime = 0;

	video.setPalette(palette);

#ifdef OJ_SCALED_CANVAS
	skyDamage = 0;
#endif

	while (true) {

		int ret = loop(pmenu, option, pmessage);
		if (ret < 0) return ret;

		// Check if level has been won
		if (returnTime && (ticks > returnTime)) {

			if (localPlayer->getJJ1BonusLevelPlayer()->getGems() >= items) {

				if (playScene("BONUS.0SC") == E_QUIT) return E_QUIT;

				return WON;

			}

			return LOST;

		}


		// Process frame-by-frame activity

		while ((getTimeChange() >= T_STEP) && (stage == LS_NORMAL)) {

			ret = step();
			steps++;
			if (ret < 0) return ret;
			else if (ret) {

				stage = LS_END;
				paletteEffects = new WhiteOutPaletteEffect(T_BONUS_END, paletteEffects);
				returnTime = ticks + T_BONUS_END;

			}

		}


		// Draw the graphics

		if ((ticks < returnTime) && !paused) direction += (ticks - prevTicks) * T_BONUS_END / (returnTime - ticks);

		draw();

		// If paused, draw "PAUSE"
		if (pmessage && !pmenu)
			font->showString("pause", (canvasW >> 1) - 44, 32);


		// Draw statistics, menu etc.
		drawOverlay(0, pmenu, option, 0, 31, 16);

#ifdef OJ_SCALED_CANVAS
		// These were drawn over the sky
		if (pmenu || pmessage || stats) {
			skyDamage = 0;
			SDL_GBA_KeepRows(0, 0);
		}
#endif

	}

	return E_NONE;

}
