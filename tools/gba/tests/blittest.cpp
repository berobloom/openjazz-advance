/*
 * blittest: random gbaBlit calls compared with a plain reference loop.
 *
 * Built on the host against src/platforms/gba/blit_gba.iwram.cpp.
 * Optional argument: number of cases (default 200000).
 */
#include "blit_gba.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static void ref (const uint8_t* src, int sp, uint8_t* dst, int dp, int w, int h, int key, const uint8_t* remap) {
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		uint8_t p = src[y * sp + x];
		if (key >= 0 && p == key) continue;
		dst[y * dp + x] = remap ? remap[p] : p;
	}
}

int main (int argc, char** argv) {
	const int cases = argc > 1 ? atoi(argv[1]) : 200000;
	static uint8_t srcBuf[64 * 80 + 64], a[96 * 80 + 64], b[96 * 80 + 64], remap[256];
	srand(1);
	for (int i = 0; i < 256; i++) remap[i] = rand();
	for (int t = 0; t < cases; t++) {
		int w = rand() % 50, h = 1 + rand() % 6, so = rand() % 8, dO = rand() % 8;
		int sp = 56 + rand() % 4, dp = 88 + rand() % 4;
		int key = (rand() % 3) ? rand() % 4 : -1;
		const uint8_t* rm = (rand() % 4) ? nullptr : remap;
		// Few distinct values, so whole words are often transparent or opaque
		int mode = rand() % 3;
		for (auto& v : srcBuf) v = mode == 0 ? rand() % 4 : mode == 1 ? (rand() % 8 ? 0 : 1 + rand() % 3) : (rand() % 8 ? 5 : 0);
		for (size_t i = 0; i < sizeof a; i++) a[i] = b[i] = rand();
		gbaBlit(srcBuf + so, sp, a + dO, dp, w, h, key, rm);
		ref(srcBuf + so, sp, b + dO, dp, w, h, key, rm);
		if (memcmp(a, b, sizeof a)) { printf("mismatch t=%d w=%d h=%d so=%d do=%d key=%d remap=%d\n", t, w, h, so, dO, key, rm != nullptr); return 1; }
	}
	printf("ok: %d blits match\n", cases);
}
