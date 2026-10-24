/*
 * ojgbarun: headless GBA runner on libmgba, for testing the OpenJazz GBA port.
 *
 * Runs a ROM for a number of frames, presses buttons on a schedule, saves
 * screenshots, prints the ROM's debug log (mGBA debug registers) to stdout and
 * reports where the CPU ended up.
 *
 * ojgbarun ROM FRAMES [options]
 *   shot=FRAME:FILE.png    save a screenshot
 *   keys=FRAME:LEN:BUTTONS hold buttons for LEN frames; A B s(elect) S(tart),
 *                          D-pad r l U D, shoulders R L
 *   sav=FILE               save file
 *   fps=FROM[:TO]          count page flips from frame FROM (to TO)
 *   prof=FROM:FILE         write "pc lr" at the end of each frame from FROM on
 *   sample=FROM:N:FILE     single-step from FROM and write "pc lr" every N
 *                          instructions, or every N cycles if N ends in c
 *                          (slow; for short runs)
 *   dump=FRAME:FILE        dump IWRAM
 *   watch=FROM:ADDR        single-step from FROM, report each change of a word
 *   peek=ADDR              print a word at the end (hex address)
 *   logframes              start each line of the ROM's log with the frame
 *   stackfree=ADDR         count untouched stack fill words from ADDR
 *                          (pass the ROM's __iheap_start)
 *   wav=FROM:FILE          record the sound from frame FROM, 32768 Hz stereo
 */
#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/timing.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static FILE* prof = NULL;
static uint16_t lastPage = 0;
static int flips = 0, fpsFlips = 0, fpsFrom = 0, fpsTo = 0;
static int profFrom = 0;
static int logFrames = 0, frame = 0;
static struct mCore* logCore = NULL;

static void logger(struct mLogger* l, int category, enum mLogLevel level, const char* format, va_list args) {
	(void)l;
	const char* name = mLogCategoryName(category);
	int debug = name && strcmp(name, "GBA Debug") == 0;
	if (!debug && !(level & (mLOG_FATAL | mLOG_ERROR | mLOG_GAME_ERROR))) return;
	if (logFrames) printf("[%d] ", frame);
	printf(debug ? "" : "[mgba %s] ", name);
	vprintf(format, args);
	if (!debug && logCore) {
		/* Where the emulated CPU was, to find the code behind bad accesses */
		int32_t pc = 0, lr = 0;
		logCore->readRegister(logCore, "pc", &pc);
		logCore->readRegister(logCore, "lr", &lr);
		printf(" (pc=%08x lr=%08x)", (unsigned)pc, (unsigned)lr);
	}
	printf("\n");
	fflush(stdout);
}

#define WAV_RATE 32768

static void putLe(FILE* f, uint32_t v, int bytes) { for (int i = 0; i < bytes; i++) fputc((v >> (8 * i)) & 0xFF, f); }

static void wavHeader(FILE* f, uint32_t dataBytes) {
	fwrite("RIFF", 1, 4, f); putLe(f, 36 + dataBytes, 4); fwrite("WAVEfmt ", 1, 8, f);
	putLe(f, 16, 4); putLe(f, 1, 2); putLe(f, 2, 2); putLe(f, WAV_RATE, 4); putLe(f, WAV_RATE * 4, 4);
	putLe(f, 4, 2); putLe(f, 16, 2); fwrite("data", 1, 4, f); putLe(f, dataBytes, 4);
}

/* Print the words on the stack that look like ROM code addresses */
static void stackCode(struct mCore* core, uint32_t sp) {
	for (uint32_t a = sp; a < 0x03008000 && a < sp + 0x1000; a += 4) {
		uint32_t v = core->busRead32(core, a);
		if ((v & 0xFE000000) == 0x08000000) printf(" %08x", v);
	}
	printf("\n");
}

static void put32(FILE* f, uint32_t v) { unsigned char b[4] = {v >> 24, v >> 16, v >> 8, v}; fwrite(b, 1, 4, f); }

static void chunk(FILE* f, const char* type, const unsigned char* data, uint32_t len) {
	put32(f, len);
	fwrite(type, 1, 4, f);
	fwrite(data, 1, len, f);
	uint32_t crc = crc32(0, (const unsigned char*)type, 4);
	crc = crc32(crc, data, len);
	put32(f, crc);
}

static void writePng(const char* path, const uint32_t* px, unsigned w, unsigned h) {
	size_t rawLen = (size_t)(w * 3 + 1) * h;
	unsigned char* raw = malloc(rawLen);
	for (unsigned y = 0; y < h; y++) {
		unsigned char* row = raw + y * (w * 3 + 1);
		row[0] = 0;
		for (unsigned x = 0; x < w; x++) {
			uint32_t c = px[y * w + x]; /* 0x00BBGGRR */
			row[1 + x * 3] = c & 0xFF; row[2 + x * 3] = (c >> 8) & 0xFF; row[3 + x * 3] = (c >> 16) & 0xFF;
		}
	}
	uLongf zLen = compressBound(rawLen);
	unsigned char* z = malloc(zLen);
	compress(z, &zLen, raw, rawLen);
	FILE* f = fopen(path, "wb");
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0};
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, zLen);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw); free(z);
}

static uint32_t parseButtons(const char* s) {
	uint32_t k = 0;
	for (; *s; s++) switch (*s) {
		case 'A': k |= 1 << 0; break; case 'B': k |= 1 << 1; break;
		case 's': k |= 1 << 2; break; case 'S': k |= 1 << 3; break;
		/* D-pad: r l U D, shoulders: R L (KEYINPUT bit order) */
		case 'r': k |= 1 << 4; break; case 'l': k |= 1 << 5; break;
		case 'U': k |= 1 << 6; break; case 'D': k |= 1 << 7; break;
		case 'R': k |= 1 << 8; break; case 'L': k |= 1 << 9; break;
	}
	return k;
}

static int dumpFrame = 0, watchFrom = 0;
static int sampleFrom = 0, sampleEvery = 0, sampleCycles = 0;
static FILE* samples = NULL;
static uint32_t peeks[16]; static int nPeeks = 0;
static uint32_t stackBottom = 0;
static const char* dumpPath = NULL;
static uint32_t watchAddr = 0;

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s ROM FRAMES [shot=F:FILE] [keys=F:LEN:BTN] [sav=FILE] [fps=FROM[:TO]] [prof=FROM:FILE]\n"
			"       [sample=FROM:N[c]:FILE] [dump=F:FILE] [watch=FROM:ADDR] [peek=ADDR] [stackfree=ADDR] [logframes]\n", argv[0]);
		return 2;
	}
	int frames = atoi(argv[2]);

	struct { int frame; char* path; } shots[64]; int nShots = 0;
	struct { int frame, len; uint32_t keys; } presses[256]; int nPresses = 0;
	const char* sav = NULL;
	const char* wavPath = NULL;
	int wavFrom = 0;
	for (int i = 3; i < argc; i++) {
		if (!strncmp(argv[i], "shot=", 5)) { shots[nShots].frame = atoi(argv[i] + 5); shots[nShots++].path = strchr(argv[i] + 5, ':') + 1; }
		else if (!strncmp(argv[i], "keys=", 5)) {
			char* p = argv[i] + 5; presses[nPresses].frame = strtol(p, &p, 10);
			presses[nPresses].len = strtol(p + 1, &p, 10); presses[nPresses++].keys = parseButtons(p + 1);
		} else if (!strncmp(argv[i], "sav=", 4)) sav = argv[i] + 4;
		else if (!strncmp(argv[i], "fps=", 4)) {
			char* p = argv[i] + 4; fpsFrom = strtol(p, &p, 10);
			if (*p == ':') fpsTo = strtol(p + 1, NULL, 10);
		}
		else if (!strncmp(argv[i], "peek=", 5)) peeks[nPeeks++] = strtoul(argv[i] + 5, NULL, 16);
		else if (!strcmp(argv[i], "logframes")) logFrames = 1;
		else if (!strncmp(argv[i], "wav=", 4)) { wavFrom = atoi(argv[i] + 4); wavPath = strchr(argv[i] + 4, ':') + 1; }
		else if (!strncmp(argv[i], "stackfree=", 10)) stackBottom = strtoul(argv[i] + 10, NULL, 16);
		else if (!strncmp(argv[i], "dump=", 5)) { dumpFrame = atoi(argv[i] + 5); dumpPath = strchr(argv[i] + 5, ':') + 1; }
		else if (!strncmp(argv[i], "watch=", 6)) {
			/* watch=FROM:ADDR single-steps from frame FROM and reports each change of the word at ADDR */
			char* p = argv[i] + 6; watchFrom = strtol(p, &p, 10); watchAddr = strtoul(p + 1, NULL, 16);
		}
		else if (!strncmp(argv[i], "sample=", 7)) {
			char* p = argv[i] + 7; sampleFrom = strtol(p, &p, 10);
			sampleEvery = strtol(p + 1, &p, 10);
			if (*p == 'c') { sampleCycles = 1; p++; }
			samples = fopen(p + 1, "w");
		}
		else if (!strncmp(argv[i], "prof=", 5)) {
			/* prof=FROM:FILE writes "pc lr" at the end of each frame from FROM on */
			char* p = argv[i] + 5; profFrom = strtol(p, &p, 10);
			prof = fopen(p + 1, "w");
		}
	}

	static struct mLogger log = { .log = logger };
	mLogSetDefaultLogger(&log);

	struct mCore* core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) { fprintf(stderr, "cannot create core\n"); return 1; }
	mCoreInitConfig(core, NULL);
	unsigned w, h;
	core->desiredVideoDimensions(core, &w, &h);
	uint32_t* buffer = calloc(w * h, 4);
	core->setVideoBuffer(core, (color_t*)buffer, w);
	if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	if (sav) {
		struct VFile* vf = VFileOpen(sav, O_CREAT | O_RDWR);
		if (vf) core->loadSave(core, vf);
	}
	core->reset(core);
	logCore = core;

	FILE* wav = NULL;
	uint32_t wavBytes = 0;
	struct blip_t* left = core->getAudioChannel(core, 0);
	struct blip_t* right = core->getAudioChannel(core, 1);
	if (wavPath) {
		core->setAudioBufferSize(core, 4096);
		blip_set_rates(left, core->frequency(core), WAV_RATE);
		blip_set_rates(right, core->frequency(core), WAV_RATE);
		wav = fopen(wavPath, "wb");
		wavHeader(wav, 0);
	}

	for (int f = 1; f <= frames; f++) {
		frame = f;
		uint32_t keys = 0;
		for (int i = 0; i < nPresses; i++)
			if (f >= presses[i].frame && f < presses[i].frame + presses[i].len) keys |= presses[i].keys;
		core->setKeys(core, keys);
		if (watchFrom && f >= watchFrom) {
			uint32_t frameStart = core->frameCounter(core);
			uint32_t old = core->busRead32(core, watchAddr);
			while (core->frameCounter(core) == frameStart) {
				int32_t pc = 0;
				core->readRegister(core, "pc", &pc);
				core->step(core);
				uint32_t now = core->busRead32(core, watchAddr);
				if (now != old) {
					int32_t lr = 0, sp = 0;
					core->readRegister(core, "lr", &lr);
					core->readRegister(core, "sp", &sp);
					printf("[runner] frame %d: %08x %08x -> %08x at pc=%08x lr=%08x sp=%08x, stack:", f, watchAddr, old, now, (unsigned)pc, (unsigned)lr, (unsigned)sp);
					stackCode(core, sp);
					old = now;
				}
			}
		} else if (samples && f >= sampleFrom) {
			uint32_t frameStart = core->frameCounter(core);
			static uint64_t nextSample = 0;
			int n = 0;
			while (core->frameCounter(core) == frameStart) {
				int32_t pc = 0, lr = 0;
				if (sampleCycles) {
					/* The instruction about to run takes the cycles until the next one */
					core->readRegister(core, "pc", &pc);
					core->readRegister(core, "lr", &lr);
					core->step(core);
					uint64_t now = mTimingGlobalTime(core->timing);
					if (!nextSample) nextSample = now + sampleEvery;
					while (now >= nextSample) {
						fprintf(samples, "%08x %08x\n", (unsigned)pc, (unsigned)lr);
						nextSample += sampleEvery;
					}
				} else {
					core->step(core);
					if (++n == sampleEvery) {
						core->readRegister(core, "pc", &pc);
						core->readRegister(core, "lr", &lr);
						fprintf(samples, "%08x %08x\n", (unsigned)pc, (unsigned)lr);
						n = 0;
					}
				}
			}
		} else core->runFrame(core);
		if (wav) {
			int16_t stereo[8192];
			int n;
			while ((n = blip_samples_avail(left)) > 0) {
				if (n > 4096) n = 4096;
				blip_read_samples(left, stereo, n, 1);
				blip_read_samples(right, stereo + 1, n, 1);
				if (f >= wavFrom) { fwrite(stereo, 4, n, wav); wavBytes += n * 4; }
			}
		}
		if (dumpPath && f == dumpFrame) {
			FILE* d = fopen(dumpPath, "wb");
			for (uint32_t a = 0x03000000; a < 0x03008000; a += 4) { uint32_t v = core->busRead32(core, a); fwrite(&v, 4, 1, d); }
			fclose(d);
		}
		/* Count Mode 4 page swaps: the engine's frame rate */
		{
			uint16_t page = core->busRead16(core, 0x04000000) & 0x10;
			if (f > 1 && page != lastPage) { flips++; if (f >= fpsFrom && (!fpsTo || f <= fpsTo)) fpsFlips++; }
			lastPage = page;
		}
		if (prof && f >= profFrom) {
			int32_t pc = 0, lr = 0;
			core->readRegister(core, "pc", &pc);
			core->readRegister(core, "lr", &lr);
			fprintf(prof, "%08x %08x\n", (unsigned)pc, (unsigned)lr);
		}
		for (int i = 0; i < nShots; i++)
			if (shots[i].frame == f) { writePng(shots[i].path, buffer, w, h); printf("[runner] frame %d -> %s\n", f, shots[i].path); }
	}

	if (wav) {
		fseek(wav, 0, SEEK_SET);
		wavHeader(wav, wavBytes);
		fclose(wav);
		printf("[runner] sound -> %s, %.1f s\n", wavPath, wavBytes / 4.0 / WAV_RATE);
	}

	if (fpsFrom) {
		int to = (fpsTo && fpsTo < frames)? fpsTo: frames;
		printf("[runner] %d page flips in frames %d-%d: %.1f fps\n", fpsFlips, fpsFrom, to,
			fpsFlips * 60.0 / (to - fpsFrom + 1));
	}

	for (int i = 0; i < nPeeks; i++) printf("[runner] peek %08x = %u\n", peeks[i], core->busRead32(core, peeks[i]));
	if (stackBottom) {
		/* stackfree=ADDR counts the stack fill pattern words from the stack's lowest address up */
		uint32_t a = (stackBottom + 3) & ~3u;
		while (a < 0x03008000 && core->busRead32(core, a) == 0x57AC57AC) a += 4;
		printf("[runner] stack never used: %u bytes\n", a - ((stackBottom + 3) & ~3u));
	}

	/* Where the CPU is, to spot hangs and crashes */
	static const char* regs[] = {"r0", "r1", "r2", "r3", "r12", "sp", "lr", "pc", "cpsr"};
	printf("[runner] end:");
	for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		int32_t value = 0;
		if (core->readRegister(core, regs[i], &value)) printf(" %s=%08x", regs[i], (unsigned)value);
	}
	printf("\n");

	/* Words on the stack that look like ROM code addresses */
	int32_t sp = 0;
	if (core->readRegister(core, "sp", &sp)) {
		printf("[runner] stack code addresses:");
		stackCode(core, sp);
	}

	if (prof) fclose(prof);
	if (samples) fclose(samples);
	core->deinit(core);
	return 0;
}
