/*
 * scaletest: random gbaBlitScaled calls compared with a full-size blit
 * followed by the scaling the GBA screen applies.
 *
 * Built on the host against src/platforms/gba/blit_gba.iwram.cpp.
 * Optional argument: number of cases (default 20000).
 */
#include "blit_gba.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static uint8_t full[200][320], stored[160][240], expect[160][240];
static uint8_t srcBuf[100 * 104 + 16], remap[256];

static void scaleDown (uint8_t out[160][240]) {
	static const int keep[3] = {0, 1, 3};
	for (int Y = 0; Y < 160; Y++) for (int X = 0; X < 240; X++)
		out[Y][X] = full[(Y * 5 + 2) >> 2][(X / 3) * 4 + keep[X % 3]];
}

int main (int argc, char** argv) {
	const int cases = argc > 1 ? atoi(argv[1]) : 20000;
	srand(2);
	for (int i = 0; i < 256; i++) remap[i] = rand();
	for (int t = 0; t < cases; t++) {
		for (auto& row : full) for (auto& v : row) v = rand();
		scaleDown(stored);
		int w = rand() % 90, h = rand() % 90, x = rand() % (320 - w + 1), y = rand() % (200 - h + 1);
		int so = rand() % 8, sp = 96 + rand() % 8;
		int key = (rand() % 3) ? rand() % 4 : -1;
		const uint8_t* rm = (rand() % 4) ? nullptr : remap;
		int mode = rand() % 3;
		for (auto& v : srcBuf) v = mode == 0 ? rand() % 4 : mode == 1 ? (rand() % 8 ? 0 : 1 + rand() % 3) : (rand() % 8 ? 5 : 0);
		const uint8_t* src = srcBuf + so;
		for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) {
			uint8_t p = src[yy * sp + xx];
			if (key >= 0 && p == key) continue;
			full[y + yy][x + xx] = rm ? rm[p] : p;
		}
		scaleDown(expect);
		gbaBlitScaled(src, sp, &stored[0][0], 240, x, y, w, h, key, rm);
		if (memcmp(stored, expect, sizeof stored)) { printf("mismatch t=%d x=%d y=%d w=%d h=%d key=%d remap=%d so=%d\n", t, x, y, w, h, key, rm != nullptr, so); return 1; }
	}
	printf("ok: %d scaled blits match\n", cases);
}
