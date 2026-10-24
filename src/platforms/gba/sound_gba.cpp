
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
 * Music and sound effects on the Game Boy Advance, in place of io/sound.cpp.
 * The bake converts the songs and sound effects into a Maxmod soundbank (see
 * dev/bakemusic.cpp), which Maxmod plays from the pack in ROM.
 *
 * Maxmod has to mix every frame, while the engine's frames can take several
 * VBlanks, so all Maxmod calls happen in the VBlank interrupt. The engine's
 * calls only leave requests for it.
 *
 */


#include "sound_gba.h"

#include "io/file.h"
#include "io/log.h"
#include "io/pack.h"
#include "io/sound.h"
#include "util.h"

#include <gba_interrupt.h>
#include <maxmod.h>
#include <algorithm>
#include <cstring>

namespace {

	const int MOD_CHANNELS = 8; ///< The most any song uses
	const int MIX_CHANNELS = 14; ///< Leaves 6 for sound effects
	const int SOUND_RATE = 11025; ///< Rate of the sound effects in the soundbank

	/* Mixing at 16 kHz left images of the sound between 8 and 16 kHz, heard as
	   static. 27 kHz takes 10% of the CPU in the menu, against 6% at 16 kHz.
	   (31 kHz needs another 320 bytes of IWRAM, which the stack can't spare.) */
	const mm_mixmode MIX_MODE = MM_MIX_27KHZ;
	const int MIX_LENGTH = MM_MIXLEN_27KHZ;

	// Maxmod's buffers. Only the mixing buffer is used heavily enough to need IWRAM.
	u8 mixMemory[MIX_LENGTH] __attribute__((aligned(4)));
	OJ_SLOW_DATA u8 waveMemory[MIX_LENGTH] __attribute__((aligned(4)));
	OJ_SLOW_DATA u8 modChannels[MOD_CHANNELS * MM_SIZEOF_MODCH] __attribute__((aligned(4)));
	OJ_SLOW_DATA u8 activeChannels[MIX_CHANNELS * MM_SIZEOF_ACTCH] __attribute__((aligned(4)));
	OJ_SLOW_DATA u8 mixChannels[MIX_CHANNELS * MM_SIZEOF_MIXCH] __attribute__((aligned(4)));

	bool audioOpen = false;

	// Soundbank index (SOUNDBNK.IDX)
	const char* songNames = nullptr; ///< Song file names, one per line, in soundbank order
	int songNamesLength = 0;
	int firstSound = 0; ///< Sample number of the first sound effect

	// Raw sound effects, in SOUNDS.000 order
	const int MAX_RAW_SOUNDS = SE::MAX;
	OJ_SLOW_DATA char rawNames[MAX_RAW_SOUNDS][13];
	int nRawSounds = 0;

	/// What each sound effect plays: a raw sound, at a rate
	struct Sound {
		int raw; ///< -1 for none
		mm_hword rate;
	};
	OJ_SLOW_DATA Sound sounds[SE::MAX];

	// Settings, as the engine sees them
	int musicVolume = MAX_VOLUME >> 1;
	int soundVolume = MAX_VOLUME >> 2;
	MusicTempo musicTempo = MusicTempo::NORMAL;
	OJ_SLOW_DATA char currentMusic[16];

	// Requests for the interrupt handler
	volatile int requestedSong = -1; ///< -1: none
	volatile bool songChanged = false;
	volatile bool musicPaused = false;
	volatile bool settingsChanged = false;
	volatile u32 requestedSounds = 0; ///< Bit n - 1 for sound effect n

	// Interrupt handler state
	bool playerPaused = false;
	bool playing = false;
	OJ_SLOW_DATA mm_sfxhand handles[SE::MAX];
	volatile u32 playingSounds = 0; ///< Bit n - 1 for sound effect n
	bool inFrame = false;

	/// Find the soundbank number of a song
	int findSong (const char* fileName) {

		const char* line = songNames;
		const char* end = songNames + songNamesLength;
		int index = 0;

		while (line < end) {
			const char* next = static_cast<const char*>(memchr(line, '\n', end - line));
			if (!next) next = end;
			int length = next - line;
			if (static_cast<int>(strlen(fileName)) == length && !strncasecmp(fileName, line, length))
				return index;
			line = next + 1;
			index++;
		}

		return -1;

	}

	/// The pack's copy of a whole file
	const unsigned char* packFile (const char* name, uint32_t& size) {

		const BakePackFile* file = packFindFile(name);
		if (!file || !file->size) return nullptr;

		uint32_t start, end;
		const unsigned char* data = packSpan(file, 0, start, end);
		if (!data || start != 0 || end != file->size) return nullptr;

		size = file->size;
		return data;

	}

}


void openAudio () {

	uint32_t bankSize, indexSize;
	const unsigned char* bank = packFile("SOUNDBNK.BIN", bankSize);
	const unsigned char* index = packFile("SOUNDBNK.IDX", indexSize);

	if (!bank || !index) {
		LOG_WARN("No soundbank in the game data pack, so no sound");
		return;
	}

	const char* text = reinterpret_cast<const char*>(index);
	const char* line = static_cast<const char*>(memchr(text, '\n', indexSize));
	if (!line) return;
	firstSound = atoi(text);
	songNames = line + 1;
	songNamesLength = text + indexSize - songNames;

	for (int i = 0; i < SE::MAX; i++) {
		sounds[i].raw = -1;
		handles[i] = 0;
	}

	// Maxmod reads the mixing mode as a word, but the enum is one byte
	mm_gba_system setup;
	memset(&setup, 0, sizeof(setup));
	setup.mixing_mode = MIX_MODE;
	setup.mod_channel_count = MOD_CHANNELS;
	setup.mix_channel_count = MIX_CHANNELS;
	setup.module_channels = modChannels;
	setup.active_channels = activeChannels;
	setup.mixing_channels = mixChannels;
	setup.mixing_memory = mixMemory;
	setup.wave_memory = waveMemory;
	setup.soundbank = const_cast<unsigned char*>(bank);
	mmInit(&setup);

	audioOpen = true;
	settingsChanged = true;

	loadSounds("SOUNDS.000");

}


void closeAudio () {

	stopMusic();

}


void gbaSoundVBlank () {

	/* Maxmod's mixer sets itself up over the VBlanks during mmInit(), and
	   mmVBlank() does nothing until then */
	mmVBlank();

}


void gbaSoundFrame () {

	// A frame still mixing when the next VBlank comes finishes first
	if (!audioOpen || inFrame) return;
	inFrame = true;

	if (songChanged) {
		songChanged = false;
		if (playing) mmStop();
		playing = requestedSong >= 0;
		if (playing) mmStart(requestedSong, MM_PLAY_LOOP);
		playerPaused = false;
		settingsChanged = true;
	}

	if (playing && musicPaused != playerPaused) {
		playerPaused = musicPaused;
		if (playerPaused) mmPause();
		else mmResume();
	}

	if (settingsChanged) {
		settingsChanged = false;
		mmSetModuleVolume(musicVolume * 1024 / MAX_VOLUME);
		mmSetEffectsVolume(soundVolume * 1024 / MAX_VOLUME);
		// The desktop game makes ticks 0.66 times as long
		mmSetModuleTempo(musicTempo == MusicTempo::FAST ? 1552 : 1024);
	}

	u32 requested = requestedSounds;
	requestedSounds = 0;

	for (int i = 1; requested; i++, requested >>= 1) {
		if (!(requested & 1)) continue;

		// Each sound effect plays once at a time, starting over if repeated
		if (handles[i]) mmEffectCancel(handles[i]);
		handles[i] = 0;

		if (sounds[i].raw < 0) continue;

		mm_sound_effect effect;
		effect.id = firstSound + sounds[i].raw;
		effect.rate = sounds[i].rate;
		effect.handle = 0;
		effect.volume = 255;
		effect.panning = 128;
		handles[i] = mmEffectEx(&effect);
	}

	u32 active = 0;
	for (int i = 1; i < SE::MAX; i++) {
		if (handles[i] && mmEffectActive(handles[i])) active |= 1u << (i - 1);
		else handles[i] = 0;
	}
	playingSounds = active;

	mmFrame();

	inFrame = false;

}


void playMusic (const char *fileName, bool restart) {

	if (!audioOpen) return;

	if (!strcasecmp(fileName, currentMusic) && !restart) return;

	int song = findSong(fileName);
	if (song < 0) {
		LOG_ERROR("Could not play music file: %s", fileName);
		stopMusic();
		return;
	}

	snprintf(currentMusic, sizeof(currentMusic), "%s", fileName);
	musicPaused = false;
	requestedSong = song;
	songChanged = true;

}


void pauseMusic (bool pause) {

	musicPaused = pause;

}


void stopMusic () {

	currentMusic[0] = 0;
	requestedSong = -1;
	songChanged = true;

}


int getMusicVolume () {

	return musicVolume;

}


void setMusicVolume (int volume) {

	musicVolume = CLAMP(volume, 0, MAX_VOLUME);
	settingsChanged = true;

}


MusicTempo getMusicTempo () {

	return musicTempo;

}


void setMusicTempo (MusicTempo tempo) {

	musicTempo = tempo;
	settingsChanged = true;

}


/**
 * Read the names of the sound effects. Their samples are in the soundbank.
 */
bool loadSounds (const char *fileName) {

	if (!audioOpen) return false;

	FilePtr file;

	try {
		file = std::make_unique<File>(fileName, PATH_TYPE_GAME);
	} catch (int e) {
		return false;
	}

	file->seek(file->getSize() - 4, true);
	int headerOffset = file->loadInt();
	nRawSounds = std::min((file->getSize() - headerOffset) / 18, MAX_RAW_SOUNDS);

	for (int i = 0; i < nRawSounds; i++) {
		file->seek(headerOffset + (i * 18), true);
		char* name = file->loadString(12);
		snprintf(rawNames[i], sizeof(rawNames[i]), "%s", name);
		delete[] name;
	}

	resampleSounds();

	return true;

}


void resampleSound (int index, const char* name, int rate) {

	// Skip SE::NONE
	int se = index + 1;
	if (!isValidSoundIndex(static_cast<SE::Type>(se))) return;

	sounds[se].raw = -1;

	for (int i = 0; i < nRawSounds; i++) {
		if (!strcmp(name, rawNames[i])) {
			sounds[se].raw = i;
			sounds[se].rate = rate * 1024 / SOUND_RATE;
			return;
		}
	}

	if (name[0]) LOG_WARN("Cannot find sound %s!", name);

}


void resampleSounds () {

	for (int i = 0; i < nRawSounds; i++)
		resampleSound(i, rawNames[i], SOUND_RATE);

}


void freeSounds () {}


void playSound (SE::Type se) {

	if (!audioOpen || se == SE::NONE || !isValidSoundIndex(se)) return;

	u16 ime = REG_IME;
	REG_IME = 0;
	requestedSounds = requestedSounds | (1u << (se - 1));
	// Playing as far as the engine can tell, until the interrupt handler knows better
	playingSounds = playingSounds | (1u << (se - 1));
	REG_IME = ime;

}


bool isSoundPlaying (SE::Type se) {

	if (!audioOpen || se == SE::NONE || !isValidSoundIndex(se)) return false;

	return playingSounds & (1u << (se - 1));

}


int getSoundVolume () {

	return soundVolume;

}


void setSoundVolume (int volume) {

	soundVolume = CLAMP(volume, 0, MAX_VOLUME);
	settingsChanged = true;

}
