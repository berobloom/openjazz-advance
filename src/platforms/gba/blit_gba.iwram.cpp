
/**
 *
 * @file blit_gba.iwram.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Pixel loops of the SDL stand-in. The file name puts them in IWRAM
 * as ARM code. Source pixels are often in cartridge ROM, where a word costs
 * little more than a byte, so they are read a word at a time.
 *
 */


#include "blit_gba.h"

namespace {

	template <bool KEYED, bool REMAP>
	inline void put (uint8_t* d, uint32_t p, uint32_t key, const uint8_t* remap) {
		if (KEYED && p == key) return;
		*d = REMAP? remap[p]: p;
	}

	template <bool KEYED, bool REMAP>
	void blitRows (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
		int w, int h, uint32_t key, const uint8_t* remap) {

		const uint32_t keys = key * 0x01010101;

		for (int y = 0; y < h; y++) {
			const uint8_t* s = src;
			uint8_t* d = dst;
			int n = w;

			while (n && (reinterpret_cast<uintptr_t>(s) & 3)) {
				put<KEYED, REMAP>(d++, *s++, key, remap);
				n--;
			}

			const uint32_t* sw = reinterpret_cast<const uint32_t*>(s);
			const bool wordStores = !REMAP && !(reinterpret_cast<uintptr_t>(d) & 3);
			for (; n >= 4; n -= 4) {
				uint32_t v = *sw++;

				if (KEYED) {
					// Skip four transparent pixels, store four opaque ones at once
					uint32_t m = v ^ keys;
					if (!m) {
						d += 4;
						continue;
					}
					if (wordStores && !((m - 0x01010101) & ~m & 0x80808080)) {
						*reinterpret_cast<uint32_t*>(d) = v;
						d += 4;
						continue;
					}
				}

				put<KEYED, REMAP>(d, v & 0xFF, key, remap);
				put<KEYED, REMAP>(d + 1, (v >> 8) & 0xFF, key, remap);
				put<KEYED, REMAP>(d + 2, (v >> 16) & 0xFF, key, remap);
				put<KEYED, REMAP>(d + 3, v >> 24, key, remap);
				d += 4;
			}

			s = reinterpret_cast<const uint8_t*>(sw);
			while (n--) put<KEYED, REMAP>(d++, *s++, key, remap);

			src += srcPitch;
			dst += dstPitch;
		}

	}

	/// Plain copy, by words where source and destination line up
	void copyRows (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch, int w, int h) {

		if ((reinterpret_cast<uintptr_t>(src) ^ reinterpret_cast<uintptr_t>(dst)) & 3) {
			blitRows<false, false>(src, srcPitch, dst, dstPitch, w, h, 0, nullptr);
			return;
		}

		for (int y = 0; y < h; y++) {
			const uint8_t* s = src;
			uint8_t* d = dst;
			int n = w;

			while (n && (reinterpret_cast<uintptr_t>(s) & 3)) {
				*d++ = *s++;
				n--;
			}

			const uint32_t* sw = reinterpret_cast<const uint32_t*>(s);
			uint32_t* dw = reinterpret_cast<uint32_t*>(d);
			for (; n >= 4; n -= 4) *dw++ = *sw++;

			s = reinterpret_cast<const uint8_t*>(sw);
			d = reinterpret_cast<uint8_t*>(dw);
			while (n--) *d++ = *s++;

			src += srcPitch;
			dst += dstPitch;
		}

	}

}


void gbaBlit (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
	int w, int h, int key, const uint8_t* remap) {

	if (key >= 0) {
		if (remap) blitRows<true, true>(src, srcPitch, dst, dstPitch, w, h, key, remap);
		else blitRows<true, false>(src, srcPitch, dst, dstPitch, w, h, key, nullptr);
	} else {
		if (remap) blitRows<false, true>(src, srcPitch, dst, dstPitch, w, h, 0, remap);
		else copyRows(src, srcPitch, dst, dstPitch, w, h);
	}

}


void gbaScaleFrame (const uint8_t* src, uint16_t* dst) {

	uint32_t* d = reinterpret_cast<uint32_t*>(dst);

	for (int y = 0; y < 160; y++) {
		// Rows 0, 1, 3, 4 of every five, nearest to y * 5 / 4
		const uint32_t* s = reinterpret_cast<const uint32_t*>(src + ((y * 5 + 2) >> 2) * 320);

		// Columns 0, 1, 3 of every four: sixteen pixels become three words
		for (int x = 0; x < 20; x++) {
			uint32_t a = s[0];
			uint32_t b = s[1];
			uint32_t c = s[2];
			uint32_t e = s[3];
			s += 4;
			d[0] = (a & 0xFFFF) | ((a >> 8) & 0xFF0000) | (b << 24);
			d[1] = ((b >> 8) & 0xFF) | ((b >> 16) & 0xFF00) | (c << 16);
			d[2] = (c >> 24) | ((e & 0xFFFF) << 8) | (e & 0xFF000000);
			d += 3;
		}
	}

}
