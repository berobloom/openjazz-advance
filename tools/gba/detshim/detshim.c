/*
 * detshim: LD_PRELOAD shim for a deterministic run of the SDL2 desktop build,
 * to check that engine changes leave the game's output alone.
 *
 * Each SDL_GetTicks call on the main thread advances time by 10 ms and time()
 * is fixed, so the menu demos play the same way on every run. Every presented
 * frame's RGB bytes are hashed and written to DET_OUT as "frame hash" lines;
 * the program exits after DET_FRAMES frames. DET_DUMP=",N,M," also saves
 * those frames as DET_OUT.N.ppm.
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
