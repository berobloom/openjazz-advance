
/**
 *
 * @file bakemusic.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Developer tool that converts the game's music and sound effects for
 * Maxmod, the GBA's music player, as part of the baked pack. Each PSM song is
 * loaded with libxmp, as the desktop game does, and written out as an Impulse
 * Tracker module. The sound effects from SOUNDS.000 become WAV files. devkitPro's
 * mmutil then builds both into one soundbank, which goes into the pack as
 * SOUNDBNK.BIN, with the song names in soundbank order in SOUNDBNK.IDX.
 *
 */


#include "bakemusic.h"
#include "bake.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <xmp.h>

#include "io/log.h"

namespace {

	// libxmp's effect codes (ext/xmp/src/effects.h), as made by its PSM loader
	enum {
		FX_PORTA_UP = 0x01, FX_PORTA_DN = 0x02, FX_TONEPORTA = 0x03,
		FX_VIBRATO = 0x04, FX_TONE_VSLIDE = 0x05, FX_VIBRA_VSLIDE = 0x06,
		FX_TREMOLO = 0x07, FX_SETPAN = 0x08, FX_OFFSET = 0x09,
		FX_VOLSLIDE = 0x0a, FX_JUMP = 0x0b, FX_BREAK = 0x0d,
		FX_EXTENDED = 0x0e, FX_SPEED = 0x0f, FX_FINE_VIBRATO = 0xac,
		FX_S3M_ARPEGGIO = 0xb4
	};
	enum {
		EX_F_PORTA_UP = 0x1, EX_F_PORTA_DN = 0x2, EX_GLISS = 0x3,
		EX_VIBRATO_WF = 0x4, EX_FINETUNE = 0x5, EX_PATTERN_LOOP = 0x6,
		EX_TREMOLO_WF = 0x7, EX_RETRIG = 0x9, EX_F_VSLIDE_UP = 0xa,
		EX_F_VSLIDE_DN = 0xb, EX_CUT = 0xc, EX_DELAY = 0xd, EX_PATT_DELAY = 0xe
	};

	/// IT command letters
	int cmd (char letter) {

		return letter - 'A' + 1;

	}

	struct ItEffect {
		int command, param;
	};

	std::map<int, int> unconverted; ///< libxmp effect -> times seen

	/// The IT command that does what libxmp does with a PSM effect
	ItEffect convertEffect (int fxt, int fxp) {

		switch (fxt) {

			case 0:
				if (!fxp) return {0, 0};
				break;

			case FX_PORTA_UP: return {cmd('F'), fxp};
			case FX_PORTA_DN: return {cmd('E'), fxp};
			case FX_TONEPORTA: return {cmd('G'), fxp};
			case FX_VIBRATO: return {cmd('H'), fxp};
			case FX_FINE_VIBRATO: return {cmd('U'), fxp};
			case FX_TONE_VSLIDE: return {cmd('L'), fxp};
			case FX_VIBRA_VSLIDE: return {cmd('K'), fxp};
			case FX_TREMOLO: return {cmd('R'), fxp};
			case FX_SETPAN: return {cmd('X'), fxp};
			case FX_OFFSET: return {cmd('O'), fxp};
			case FX_VOLSLIDE: return {cmd('D'), fxp};
			case FX_JUMP: return {cmd('B'), fxp};
			case FX_BREAK: return {cmd('C'), (fxp >> 4) * 10 + (fxp & 0xf)}; // BCD in libxmp
			case FX_S3M_ARPEGGIO: return {cmd('J'), fxp};

			case FX_SPEED:
				if (!fxp) return {0, 0};
				if (fxp < 0x20) return {cmd('A'), fxp};
				return {cmd('T'), fxp};

			case FX_EXTENDED: {
				int x = fxp & 0xf;
				switch (fxp >> 4) {
					case EX_F_PORTA_UP: return {cmd('F'), 0xf0 | x};
					case EX_F_PORTA_DN: return {cmd('E'), 0xf0 | x};
					case EX_GLISS: return {cmd('S'), 0x10 | x};
					case EX_FINETUNE: return {cmd('S'), 0x20 | x};
					case EX_VIBRATO_WF: return {cmd('S'), 0x30 | x};
					case EX_TREMOLO_WF: return {cmd('S'), 0x40 | x};
					case EX_PATTERN_LOOP: return {cmd('S'), 0xb0 | x};
					case EX_CUT: return {cmd('S'), 0xc0 | x};
					case EX_DELAY: return {cmd('S'), 0xd0 | x};
					case EX_PATT_DELAY: return {cmd('S'), 0xe0 | x};
					case EX_RETRIG: return {cmd('Q'), x};
					case EX_F_VSLIDE_UP: return {cmd('D'), (x << 4) | 0xf};
					case EX_F_VSLIDE_DN: return {cmd('D'), 0xf0 | x};
				}
				break;
			}

		}

		unconverted[(fxt << 8) | ((fxt == FX_EXTENDED) ? (fxp >> 4) : 0)]++;

		return {0, 0};

	}

	template <typename T>
	void put (std::string& out, T value) {

		out.append(reinterpret_cast<const char*>(&value), sizeof(value));

	}

	template <typename T>
	void poke (std::string& out, size_t at, T value) {

		memcpy(&out[at], &value, sizeof(value));

	}

	void putText (std::string& out, const char* text, size_t length) {

		size_t used = std::min(strlen(text), length);
		out.append(text, used);
		out.append(length - used, '\0');

	}

	/**
	 * Write a libxmp module as an Impulse Tracker module, using samples
	 * without instruments.
	 */
	std::string writeIt (const xmp_module* mod) {

		// IT has no restart position: the song goes back to its first order
		std::vector<int> orders(mod->xxo, mod->xxo + mod->len);
		int restart = (mod->rst > 0 && mod->rst < mod->len) ? mod->rst : 0;
		int jumpPattern = -1;

		if (restart) {
			// Copy the last pattern with a jump on its last row
			jumpPattern = mod->pat;
			orders.back() = jumpPattern;
		}

		int nPatterns = mod->pat + (jumpPattern >= 0 ? 1 : 0);
		std::string out;

		out += "IMPM";
		putText(out, mod->name, 26);
		put<uint16_t>(out, 0x1004); // row highlight
		put<uint16_t>(out, orders.size() + 1);
		put<uint16_t>(out, 0); // instruments
		put<uint16_t>(out, mod->smp);
		put<uint16_t>(out, nPatterns);
		put<uint16_t>(out, 0x0214); // made with
		put<uint16_t>(out, 0x0214); // compatible with
		put<uint16_t>(out, 0x0001 | 0x0010); // stereo, old effects (S3M depths)
		put<uint16_t>(out, 0);
		put<uint8_t>(out, std::min(mod->gvl, 64) * 2);
		put<uint8_t>(out, 48); // mixing volume
		put<uint8_t>(out, mod->spd);
		put<uint8_t>(out, mod->bpm);
		put<uint8_t>(out, 128); // panning separation
		put<uint8_t>(out, 0);
		put<uint16_t>(out, 0);
		put<uint32_t>(out, 0);
		put<uint32_t>(out, 0);

		for (int i = 0; i < 64; i++) {
			const xmp_channel& c = mod->xxc[i];
			int pan = (c.flg & XMP_CHANNEL_SURROUND) ? 100 : std::min((c.pan + 2) / 4, 64);
			put<uint8_t>(out, i < mod->chn ? pan : pan | 128); // 128: disabled
		}
		for (int i = 0; i < 64; i++)
			put<uint8_t>(out, std::min(mod->xxc[i].vol, 64));

		for (int order : orders) put<uint8_t>(out, order);
		put<uint8_t>(out, 255);

		size_t sampleTable = out.size();
		out.append(4 * (mod->smp + nPatterns), '\0');
		size_t patternTable = sampleTable + 4 * mod->smp;

		// Sample headers

		std::vector<size_t> dataPointer(mod->smp);

		for (int i = 0; i < mod->smp; i++) {

			const xmp_sample& s = mod->xxs[i];
			const xmp_subinstrument* sub = (i < mod->ins && mod->xxi[i].nsm) ? &mod->xxi[i].sub[0] : nullptr;
			bool present = s.len > 0 && s.data;
			bool loop = (s.flg & XMP_SAMPLE_LOOP) && s.lpe > s.lps;

			poke<uint32_t>(out, sampleTable + 4 * i, out.size());

			out += "IMPS";
			putText(out, "", 12);
			put<uint8_t>(out, 0);
			put<uint8_t>(out, 64); // global volume
			put<uint8_t>(out, (present ? 1 : 0) | (loop ? 0x10 : 0));
			put<uint8_t>(out, sub ? std::min(sub->vol, 64) : 64);
			putText(out, mod->xxi[i].name, 26);
			put<uint8_t>(out, 1); // signed samples
			put<uint8_t>(out, 32); // default pan, not enabled
			put<uint32_t>(out, present ? s.len : 0);
			put<uint32_t>(out, loop ? s.lps : 0);
			put<uint32_t>(out, loop ? std::min(s.lpe, s.len) : 0);

			// libxmp keeps the PSM's sample rate as a transpose from 8363 Hz
			double semitones = sub ? sub->xpo + sub->fin / 128.0 : 0;
			put<uint32_t>(out, static_cast<uint32_t>(8363.0 * pow(2.0, semitones / 12.0) + 0.5));

			put<uint32_t>(out, 0);
			put<uint32_t>(out, 0);
			dataPointer[i] = out.size();
			put<uint32_t>(out, 0);
			put<uint32_t>(out, 0); // vibrato

		}

		// Patterns

		for (int p = 0; p < nPatterns; p++) {

			const xmp_pattern* pattern = mod->xxp[p < mod->pat ? p : mod->xxo[mod->len - 1]];
			int rows = pattern->rows;

			poke<uint32_t>(out, patternTable + 4 * p, out.size());

			std::string packed;

			for (int r = 0; r < rows; r++) {

				bool jumpRow = p == jumpPattern && r == rows - 1;
				bool jumped = false;

				for (int c = 0; c < mod->chn; c++) {

					const xmp_event& e = mod->xxt[pattern->index[c]]->event[r];
					ItEffect fx = convertEffect(e.fxt, e.fxp);

					if (jumpRow && !jumped && (!fx.command || fx.command == cmd('C'))) {
						fx = {cmd('B'), restart};
						jumped = true;
					}

					int mask = (e.note ? 1 : 0) | (e.ins ? 2 : 0) | (e.vol ? 4 : 0) | (fx.command ? 8 : 0);
					if (!mask) continue;

					packed += static_cast<char>((c + 1) | 0x80);
					packed += static_cast<char>(mask);

					if (e.note) {
						int note = e.note == XMP_KEY_OFF ? 255 : e.note == XMP_KEY_CUT ? 254 : e.note - 1;
						packed += static_cast<char>(note);
					}
					if (e.ins) packed += static_cast<char>(e.ins);
					if (e.vol) packed += static_cast<char>(std::min(e.vol - 1, 64));
					if (fx.command) {
						packed += static_cast<char>(fx.command);
						packed += static_cast<char>(fx.param);
					}

				}

				if (jumpRow && !jumped) LOG_WARN("bake: no free channel for the restart jump");

				packed += '\0';

			}

			put<uint16_t>(out, packed.size());
			put<uint16_t>(out, rows);
			put<uint32_t>(out, 0);
			out += packed;

		}

		// Sample data

		for (int i = 0; i < mod->smp; i++) {
			const xmp_sample& s = mod->xxs[i];
			if (s.len <= 0 || !s.data) continue;
			poke<uint32_t>(out, dataPointer[i], out.size());
			out.append(reinterpret_cast<const char*>(s.data), s.len);
		}

		return out;

	}

	bool writeFile (const std::string& path, const std::string& data) {

		FILE* f = fopen(path.c_str(), "wb");
		if (!f) {
			LOG_ERROR("bake: cannot write %s", path.c_str());
			return false;
		}
		fwrite(data.data(), 1, data.size(), f);
		fclose(f);

		return true;

	}

	bool readFile (const std::string& path, std::string& data) {

		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return false;
		char buffer[65536];
		size_t n;
		data.clear();
		while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) data.append(buffer, n);
		fclose(f);

		return true;

	}

	std::string upper (std::string text) {

		for (char& c : text) c = toupper(static_cast<unsigned char>(c));

		return text;

	}

	/// An 8-bit mono WAV file of signed samples
	std::string writeWav (const std::string& samples, int rate) {

		std::string out = "RIFF";
		put<uint32_t>(out, 36 + samples.size());
		out += "WAVEfmt ";
		put<uint32_t>(out, 16);
		put<uint16_t>(out, 1);
		put<uint16_t>(out, 1);
		put<uint32_t>(out, rate);
		put<uint32_t>(out, rate);
		put<uint16_t>(out, 1);
		put<uint16_t>(out, 8);
		out += "data";
		put<uint32_t>(out, samples.size());
		for (char c : samples) out += static_cast<char>(c ^ 0x80); // WAV is unsigned

		return out;

	}

	/// Sound effects in the order of SOUNDS.000, as loadSounds() reads them
	bool writeSounds (const std::string& path, const std::string& workDir, std::vector<std::string>& wavs) {

		std::string data;
		if (!readFile(path, data) || data.size() < 8 || data.compare(0, 2, "sf") != 0) {
			LOG_ERROR("bake: cannot read sound effects from %s", path.c_str());
			return false;
		}

		uint32_t headerOffset;
		memcpy(&headerOffset, &data[data.size() - 4], 4);
		int nSounds = (data.size() - headerOffset) / 18;

		for (int i = 0; i < nSounds; i++) {
			const char* header = &data[headerOffset + i * 18];
			uint32_t offset;
			uint16_t length;
			memcpy(&offset, header + 12, 4);
			memcpy(&length, header + 16, 2);
			if (offset + length > data.size()) length = 0;

			char name[24];
			snprintf(name, sizeof(name), "sfx%02d.wav", i);
			if (!writeFile(workDir + "/" + name, writeWav(data.substr(offset, length), 11025))) return false;
			wavs.push_back(name);
		}

		return true;

	}

	std::string mmutilPath () {

		if (const char* path = getenv("MMUTIL")) return path;

		const char* devkitPro = getenv("DEVKITPRO");
		std::string path = std::string(devkitPro ? devkitPro : "/opt/devkitpro") + "/tools/bin/mmutil";

		struct stat info;
		return stat(path.c_str(), &info) == 0 ? path : "";

	}

}


bool bakeMusic (const char* gameDir, const char* workDir) {

	std::string mmutil = mmutilPath();
	if (mmutil.empty()) {
		LOG_WARN("bake: mmutil not found (set DEVKITPRO or MMUTIL), so the pack has no sound");
		return true;
	}

	mkdir(workDir, 0755);

	// Songs, sorted by name

	std::vector<std::pair<std::string, std::string>> songs; // pack name, path
	std::string soundsPath;

	if (DIR* dir = opendir(gameDir)) {
		while (dirent* entry = readdir(dir)) {
			std::string name = upper(entry->d_name);
			std::string path = std::string(gameDir) + "/" + entry->d_name;
			if (name.size() > 4 && name.compare(name.size() - 4, 4, ".PSM") == 0)
				songs.push_back({name, path});
			else if (name == "SOUNDS.000")
				soundsPath = path;
		}
		closedir(dir);
	}

	std::sort(songs.begin(), songs.end());

	xmp_context xmp = xmp_create_context();
	std::vector<std::string> inputs;
	std::string index;
	long itBytes = 0;

	for (auto& song : songs) {

		if (xmp_load_module(xmp, song.second.c_str()) != 0) {
			LOG_WARN("bake: libxmp cannot load %s", song.first.c_str());
			continue;
		}

		xmp_module_info info;
		xmp_get_module_info(xmp, &info);

		std::string it = writeIt(info.mod);
		std::string itName = song.first.substr(0, song.first.size() - 4) + ".it";
		bool ok = writeFile(std::string(workDir) + "/" + itName, it);
		LOG_DEBUG("bake: %s: %d channels, %d patterns, %d samples, restart %d -> %zu bytes",
			song.first.c_str(), info.mod->chn, info.mod->pat, info.mod->smp, info.mod->rst, it.size());

		xmp_release_module(xmp);
		if (!ok) return false;

		inputs.push_back(itName);
		index += song.first + "\n";
		itBytes += it.size();

	}

	xmp_free_context(xmp);

	for (auto& item : unconverted)
		printf("bake: libxmp effect %02x/%x left out %d times\n", item.first >> 8, item.first & 0xff, item.second);

	if (inputs.empty()) {
		LOG_WARN("bake: no music converted");
		return true;
	}

	std::vector<std::string> wavs;
	if (!soundsPath.empty() && !writeSounds(soundsPath, workDir, wavs)) return false;

	// mmutil numbers modules in the order given, then the WAV files as sound
	// effects in the order given. It cuts long paths short, so it runs in the
	// work directory with plain file names.

	std::string bankPath = std::string(workDir) + "/SOUNDBNK.BIN";
	remove(bankPath.c_str());
	remove((std::string(workDir) + "/soundbank.h").c_str());
	std::string command = "cd '" + std::string(workDir) + "' && '" + mmutil + "' -oSOUNDBNK.BIN -hsoundbank.h";
	for (auto& input : inputs) command += " " + input;
	for (auto& wav : wavs) command += " " + wav;
	command += " > mmutil.log 2>&1";

	if (system(command.c_str()) != 0) {
		LOG_ERROR("bake: mmutil failed, see %s/mmutil.log", workDir);
		return false;
	}

	// Check the numbering, and find the first sound effect

	std::string header;
	readFile(std::string(workDir) + "/soundbank.h", header);
	int firstSound = -1;

	for (size_t i = 0; i < inputs.size(); i++) {
		std::string define = "#define MOD_" + inputs[i].substr(0, inputs[i].size() - 3) + "\t" + std::to_string(i) + "\r\n"; // mmutil writes CRLF
		if (header.find(define) == std::string::npos) {
			LOG_ERROR("bake: mmutil numbered %s differently", inputs[i].c_str());
			return false;
		}
	}

	const char* define = wavs.empty() ? "#define MSL_NSAMPS\t" : "#define SFX_SFX00\t";
	size_t found = header.find(define);
	if (found != std::string::npos) firstSound = atoi(header.c_str() + found + strlen(define));
	if (firstSound < 0) {
		LOG_ERROR("bake: no sound effects in %s/soundbank.h", workDir);
		return false;
	}

	// SOUNDBNK.IDX: the first sound effect's sample number, then the songs
	index = std::to_string(firstSound) + "\n" + index;

	std::string indexPath = std::string(workDir) + "/SOUNDBNK.IDX";
	if (!writeFile(indexPath, index)) return false;

	struct stat bank;
	stat(bankPath.c_str(), &bank);
	printf("bake: %zu songs (%ld bytes as IT) and %zu sound effects -> %s, %ld bytes\n",
		inputs.size(), itBytes, wavs.size(), bankPath.c_str(), static_cast<long>(bank.st_size));

	bakeOpened(bankPath.c_str(), bank.st_size);
	bakeOpened(indexPath.c_str(), index.size());

	return true;

}
