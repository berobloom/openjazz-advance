/*
 * detshim: LD_PRELOAD shim for a deterministic run of the SDL2 desktop build,
 * to check that engine changes leave the game's output alone.
 *
 * Each SDL_GetTicks call on the main thread advances time by 10 ms and time()
 * is fixed, so the menu demos play the same way on every run. Every presented
 * frame's RGB bytes are hashed and written to DET_OUT as "frame hash" lines;
 * the program exits after DET_FRAMES frames. DET_DUMP=",N,M," also saves
 * those frames as DET_OUT.N.ppm. DET_KEYS="F:KEY[:LEN],..." presses the SDL
 * keycode KEY (decimal; 13 is Return) at frame F and releases it LEN frames
 * later (default 2).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <SDL2/SDL.h>

static Uint32 ticks = 1000;
static int frames = 0;
static FILE* out;

#include <sys/syscall.h>
static int mainThread (void) { return syscall(SYS_gettid) == getpid(); }
static Uint32 fakeTicks (void) { if (mainThread()) ticks += 10; return ticks; }
Uint32 SDL_GetTicks (void) { return fakeTicks(); }
Uint64 SDL_GetTicks64 (void) { return fakeTicks(); }
void SDL_Delay (Uint32 ms) { if (!mainThread()) usleep(ms * 1000); }
time_t time (time_t* t) { time_t v = 1700000000; if (t) *t = v; return v; }

/// Next key event due from DET_KEYS at the current frame, if any
static int injectKey (SDL_Event* event) {
	static int pressed[64], done[64];
	const char* keys = getenv("DET_KEYS");
	int i = 0;

	for (const char* p = keys; p && *p && i < 64; i++) {
		int frame = atoi(p);
		const char* colon = strchr(p, ':');
		if (!colon) break;
		int key = atoi(colon + 1);
		const char* comma = strchr(p, ',');
		const char* second = strchr(colon + 1, ':');
		int length = (second && (!comma || second < comma))? atoi(second + 1): 2;

		int type = 0;
		if (!pressed[i] && frames >= frame) { pressed[i] = 1; type = SDL_KEYDOWN; }
		else if (pressed[i] && !done[i] && frames >= frame + length) { done[i] = 1; type = SDL_KEYUP; }

		if (type) {
			memset(event, 0, sizeof *event);
			event->type = type;
			event->key.state = (type == SDL_KEYDOWN)? SDL_PRESSED: SDL_RELEASED;
			event->key.keysym.sym = key;
			return 1;
		}

		p = comma? comma + 1: NULL;
	}

	return 0;
}

int SDL_PollEvent (SDL_Event* event) {
	static int (*real)(SDL_Event*);
	if (!real) real = dlsym(RTLD_NEXT, "SDL_PollEvent");
	if (event && injectKey(event)) return 1;
	return real(event);
}

int SDL_UpdateTexture (SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
	static int (*real)(SDL_Texture*, const SDL_Rect*, const void*, int);
	static int (*query)(SDL_Texture*, Uint32*, int*, int*, int*);
	if (!real) {
		real = dlsym(RTLD_NEXT, "SDL_UpdateTexture");
		query = dlsym(RTLD_NEXT, "SDL_QueryTexture");
		out = fopen(getenv("DET_OUT"), "w");
	}
	int w, h;
	query(texture, NULL, NULL, &w, &h);
	uint64_t hash = 1469598103934665603ULL;
	for (int y = 0; y < h; y++) {
		const uint8_t* row = (const uint8_t*)pixels + y * pitch;
		for (int x = 0; x < w * 4; x++) if ((x & 3) != 3) hash = (hash ^ row[x]) * 1099511628211ULL;
	}
	fprintf(out, "%d %016llx\n", frames, (unsigned long long)hash);
	if (getenv("DET_DUMP")) {
		char key[32]; snprintf(key, sizeof key, ",%d,", frames);
		if (strstr(getenv("DET_DUMP"), key)) {
			char name[512]; snprintf(name, sizeof name, "%s.%d.ppm", getenv("DET_OUT"), frames);
			FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", w, h);
			for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
				const uint8_t* p = (const uint8_t*)pixels + y * pitch + x * 4;
				fputc(p[2], f); fputc(p[1], f); fputc(p[0], f);
			}
			fclose(f);
		}
	}
	if (++frames >= atoi(getenv("DET_FRAMES"))) { fclose(out); _exit(0); }
	return real(texture, rect, pixels, pitch);
}
