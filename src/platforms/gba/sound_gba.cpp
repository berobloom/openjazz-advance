
/**
 *
 * @file sound_gba.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Silent stand-in for io/sound.cpp on the Game Boy Advance, until music and
 * sound effects are converted for Maxmod. Settings are kept so menus work.
 *
 */


#include "io/sound.h"

namespace {
	int musicVolume = MAX_VOLUME;
	int soundVolume = MAX_VOLUME;
	MusicTempo musicTempo = MusicTempo::NORMAL;
}

void openAudio () {}
void closeAudio () {}

void playMusic (const char *fileName, bool restart) {
	(void)fileName;
	(void)restart;
}

void pauseMusic (bool pause) {
	(void)pause;
}

void stopMusic () {}

int getMusicVolume () {
	return musicVolume;
}

void setMusicVolume (int volume) {
	musicVolume = volume;
}

MusicTempo getMusicTempo () {
	return musicTempo;
}

void setMusicTempo (MusicTempo tempo) {
	musicTempo = tempo;
}

bool loadSounds (const char *fileName) {
	(void)fileName;
	return true;
}

void resampleSound (int index, const char* name, int rate) {
	(void)index;
	(void)name;
	(void)rate;
}

void resampleSounds () {}
void freeSounds () {}

void playSound (SE::Type se) {
	(void)se;
}

bool isSoundPlaying (SE::Type se) {
	(void)se;
	return false;
}

int getSoundVolume () {
	return soundVolume;
}

void setSoundVolume (int volume) {
	soundVolume = volume;
}
