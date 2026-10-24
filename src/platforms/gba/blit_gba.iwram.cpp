
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

#include <cstring>

namespace {

	template <bool KEYED, bool REMAP>
	inline void put (uint8_t* d, uint32_t p, uint32_t key, const uint8_t* remap) {
		if (KEYED && p == key) return;
		*d = REMAP? remap[p]: p;
	}

	/// Rows through a remapping table, reading the source a word at a time
	template <bool KEYED>
	void remapRows (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
		int w, int h, uint32_t key, const uint8_t* remap) {

		const uint32_t keys = key * 0x01010101;

		for (int y = 0; y < h; y++) {
			const uint8_t* s = src;
			uint8_t* d = dst;
			int n = w;

			while (n && (reinterpret_cast<uintptr_t>(s) & 3)) {
				put<KEYED, true>(d++, *s++, key, remap);
				n--;
			}

			const uint32_t* sw = reinterpret_cast<const uint32_t*>(s);
			for (; n >= 4; n -= 4) {
				uint32_t v = *sw++;

				// Skip four transparent pixels at once
				if (KEYED && v == keys) {
					d += 4;
					continue;
				}

				put<KEYED, true>(d, v & 0xFF, key, remap);
				put<KEYED, true>(d + 1, (v >> 8) & 0xFF, key, remap);
				put<KEYED, true>(d + 2, (v >> 16) & 0xFF, key, remap);
				put<KEYED, true>(d + 3, v >> 24, key, remap);
				d += 4;
			}

			s = reinterpret_cast<const uint8_t*>(sw);
			while (n--) put<KEYED, true>(d++, *s++, key, remap);

			src += srcPitch;
			dst += dstPitch;
		}

	}

	/// Store the opaque pixels of a word that also has transparent ones
	__attribute__((noinline)) void putMixed (uint32_t* d, uint32_t v, uint32_t opaque) {
		// Reading the destination back to merge costs more than byte stores
		uint8_t* b = reinterpret_cast<uint8_t*>(d);
		if (opaque & 0xFF) b[0] = v;
		if (opaque & 0xFF00) b[1] = v >> 8;
		if (opaque & 0xFF0000) b[2] = v >> 16;
		if (opaque & 0xFF000000) b[3] = v >> 24;
	}

	/**
	 * Store a word of pixels, leaving the transparent ones out. Tile words are
	 * mostly all transparent or all opaque, which take the fewest tests.
	 */
	template <bool KEYED>
	inline void putWord (uint32_t* d, uint32_t v, uint32_t keys) {
		if (!KEYED) {
			*d = v;
			return;
		}

		// Bytes that differ from the key, i.e. are opaque, are non-zero
		uint32_t x = v ^ keys;
		if (!x) return;

		if ((x & 0xFF) && (x & 0xFF00) && (x & 0xFF0000) && (x & 0xFF000000)) {
			*d = v;
			return;
		}

		putMixed(d, v, x);
	}

	/**
	 * Copy rows a destination word at a time. Where source and destination
	 * do not line up, each word is put together from two source words.
	 */
	template <bool KEYED>
	void wordRows (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
		int w, int h, uint32_t key) {

		const uint32_t keys = key * 0x01010101;

		for (int y = 0; y < h; y++) {
			const uint8_t* s = src;
			uint8_t* d = dst;
			int n = w;

			while (n && (reinterpret_cast<uintptr_t>(d) & 3)) {
				put<KEYED, false>(d++, *s++, key, nullptr);
				n--;
			}

			uint32_t* dw = reinterpret_cast<uint32_t*>(d);
			int words = n >> 2;
			unsigned int shift = (reinterpret_cast<uintptr_t>(s) & 3) << 3;
			const uint32_t* sw = reinterpret_cast<const uint32_t*>(s - (shift >> 3));

			if (!shift) {
				for (int i = 0; i < words; i++) putWord<KEYED>(dw++, *sw++, keys);
			} else if (words) {
				// Only reads the word holding the last byte needed, no further
				uint32_t prev = *sw++;
				for (int i = 0; i < words; i++) {
					uint32_t next = *sw++;
					putWord<KEYED>(dw++, (prev >> shift) | (next << (32 - shift)), keys);
					prev = next;
				}
			}

			s += words << 2;
			d += words << 2;
			n &= 3;
			while (n--) put<KEYED, false>(d++, *s++, key, nullptr);

			src += srcPitch;
			dst += dstPitch;
		}

	}

}


void gbaBlit (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
	int w, int h, int key, const uint8_t* remap) {

	if (key >= 0) {
		if (remap) remapRows<true>(src, srcPitch, dst, dstPitch, w, h, key, remap);
		else wordRows<true>(src, srcPitch, dst, dstPitch, w, h, key);
	} else {
		if (remap) remapRows<false>(src, srcPitch, dst, dstPitch, w, h, 0, remap);
		else wordRows<false>(src, srcPitch, dst, dstPitch, w, h, 0);
	}

}


namespace {
	/// Canvas column of a stored column, as scaledColumnOf() in platforms/gba.h
	inline int canvasColumn (int column) {
		int j = column % 3;
		return ((column / 3) << 2) + j + (j >> 1);
	}

	/**
	 * Where each row of a block goes on a scaled surface. All rows keep the
	 * same columns, so this is worked out once per block. Source offsets are
	 * in canvas columns from the block's first column.
	 */
	struct ScaledSpan {
		int first;          ///< First stored column
		int n;              ///< Stored columns
		int head;           ///< Stored columns before the first whole destination word
		int headSource[3];
		int words;          ///< Whole destination words
		int phase;          ///< Stored column of the first word, modulo 3
		int wordSource;     ///< Start of the source group holding the first word's first pixel
		int tail;           ///< Stored columns after the last whole word
		int tailSource[3];
	};

	ScaledSpan scaledSpan (int x, int w) {
		// The same mapping as scaledColumns() in platforms/gba.h
		auto columns = [](int c) { int r = c & 3; return (3 * (c >> 2)) + r - (r == 3); };

		ScaledSpan span;
		span.first = columns(x);
		span.n = columns(x + w) - span.first;
		if (span.n <= 0) {
			span.n = 0;
			return span;
		}

		int column = span.first;
		int n = span.n;

		// The surface's rows start on word boundaries
		span.head = (-column) & 3;
		if (span.head > n) span.head = n;
		for (int i = 0; i < span.head; i++) span.headSource[i] = canvasColumn(column++) - x;
		n -= span.head;

		span.words = n >> 2;
		span.phase = column % 3;
		span.wordSource = ((column / 3) << 2) - x;
		column += span.words << 2;

		span.tail = n & 3;
		for (int i = 0; i < span.tail; i++) span.tailSource[i] = canvasColumn(column++) - x;

		return span;
	}

	/**
	 * Whole destination words of one row. Three words take their twelve pixels
	 * from four source words, keeping canvas columns 0, 1 and 3 of each.
	 *
	 * @param g Start of the source group holding the first word's first pixel
	 * @param SHIFT Bits by which g lies past a word boundary
	 */
	template <bool KEYED, unsigned int SHIFT>
	__attribute__((noinline)) void scaledWords (const uint8_t* g, uint32_t* d, int phase, int words, uint32_t keys) {

		const uint32_t* gw = reinterpret_cast<const uint32_t*>(g - (SHIFT >> 3));
		uint32_t prev = SHIFT? *gw++: 0;

		// Next four canvas pixels. Only reads the word holding the last byte needed.
		auto fetch = [&]() -> uint32_t {
			if (!SHIFT) return *gw++;
			uint32_t following = *gw++;
			uint32_t v = (prev >> SHIFT) | (following << ((32 - SHIFT) & 31));
			prev = following;
			return v;
		};

		// The three ways a word's pixels lie in two source words
		auto first = [](uint32_t a, uint32_t b) { return (a & 0xFFFF) | ((a >> 8) & 0xFF0000) | (b << 24); };
		auto second = [](uint32_t a, uint32_t b) { return ((a >> 8) & 0xFF) | ((a >> 16) & 0xFF00) | (b << 16); };
		auto third = [](uint32_t a, uint32_t b) { return (a >> 24) | ((b & 0xFFFF) << 8) | (b & 0xFF000000); };

		if (phase == 1) {
			uint32_t a = fetch();
			uint32_t b = fetch();
			putWord<KEYED>(d++, second(a, b), keys);
			if (!--words) return;
			putWord<KEYED>(d++, third(b, fetch()), keys);
			words--;
		} else if (phase == 2) {
			uint32_t a = fetch();
			putWord<KEYED>(d++, third(a, fetch()), keys);
			words--;
		}

		for (; words >= 3; words -= 3) {
			uint32_t a = fetch();
			uint32_t b = fetch();
			uint32_t c = fetch();
			uint32_t e = fetch();
			putWord<KEYED>(d, first(a, b), keys);
			putWord<KEYED>(d + 1, second(b, c), keys);
			putWord<KEYED>(d + 2, third(c, e), keys);
			d += 3;
		}

		if (words) {
			uint32_t a = fetch();
			uint32_t b = fetch();
			putWord<KEYED>(d, first(a, b), keys);
			if (words == 2) putWord<KEYED>(d + 1, second(b, fetch()), keys);
		}

	}

	template <bool KEYED, bool REMAP>
	void scaledRows (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
		int x, int y, int w, int h, uint32_t key, const uint8_t* remap) {

		// The same mapping as scaledRows() in platforms/gba.h
		auto rows = [](int c) { int r = c % 5; return ((c / 5) << 2) + r - (r >= 3); };

		const ScaledSpan span = scaledSpan(x, w);
		if (!span.n) return;

		const uint32_t keys = key * 0x01010101;
		int lastRow = rows(y + h);

		for (int row = rows(y); row < lastRow; row++) {
			const uint8_t* s = src + (((((row * 5) + 2) >> 2) - y) * srcPitch);
			uint8_t* d = dst + (row * dstPitch) + span.first;

			if (REMAP) {
				for (int i = 0; i < span.n; i++)
					put<KEYED, true>(d + i, s[canvasColumn(span.first + i) - x], key, remap);
				continue;
			}

			for (int i = 0; i < span.head; i++) put<KEYED, false>(d++, s[span.headSource[i]], key, nullptr);

			if (span.words) {
				const uint8_t* g = s + span.wordSource;
				uint32_t* dw = reinterpret_cast<uint32_t*>(d);
				switch (reinterpret_cast<uintptr_t>(g) & 3) {
					case 0: scaledWords<KEYED, 0>(g, dw, span.phase, span.words, keys); break;
					case 1: scaledWords<KEYED, 8>(g, dw, span.phase, span.words, keys); break;
					case 2: scaledWords<KEYED, 16>(g, dw, span.phase, span.words, keys); break;
					default: scaledWords<KEYED, 24>(g, dw, span.phase, span.words, keys); break;
				}
				d += span.words << 2;
			}

			for (int i = 0; i < span.tail; i++) put<KEYED, false>(d++, s[span.tailSource[i]], key, nullptr);
		}

	}

}


void gbaBlitScaled (const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch,
	int x, int y, int w, int h, int key, const uint8_t* remap) {

	if (key >= 0) {
		if (remap) scaledRows<true, true>(src, srcPitch, dst, dstPitch, x, y, w, h, key, remap);
		else scaledRows<true, false>(src, srcPitch, dst, dstPitch, x, y, w, h, key, nullptr);
	} else {
		if (remap) scaledRows<false, true>(src, srcPitch, dst, dstPitch, x, y, w, h, 0, remap);
		else scaledRows<false, false>(src, srcPitch, dst, dstPitch, x, y, w, h, 0, nullptr);
	}

}


void gbaFill (uint8_t* dst, int pitch, int w, int h, uint8_t color) {

	const uint32_t v = color * 0x01010101;

	for (int y = 0; y < h; y++) {
		uint8_t* d = dst;
		int n = w;

		while (n && (reinterpret_cast<uintptr_t>(d) & 3)) {
			*d++ = color;
			n--;
		}

		uint32_t* dw = reinterpret_cast<uint32_t*>(d);
		for (; n >= 16; n -= 16) {
			dw[0] = v;
			dw[1] = v;
			dw[2] = v;
			dw[3] = v;
			dw += 4;
		}
		for (; n >= 4; n -= 4) *dw++ = v;

		d = reinterpret_cast<uint8_t*>(dw);
		while (n--) *d++ = color;

		dst += pitch;
	}

}


void gbaCopyWords (const void* src, void* dst, int words) {

#ifdef __arm__
	// Eight words per burst, as fast as DMA from EWRAM
	asm volatile (
		"1:\n"
		"ldmia %0!, {r3-r10}\n"
		"stmia %1!, {r3-r10}\n"
		"subs %2, %2, #8\n"
		"bgt 1b\n"
		: "+r" (src), "+r" (dst), "+r" (words)
		:
		: "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "cc", "memory");
#else
	// For the blit tests on the PC
	memcpy(dst, src, words * 4);
#endif

}


void gbaFillWords (void* dst, uint32_t value, int words) {

#ifdef __arm__
	asm volatile (
		"mov r3, %2\n"
		"mov r4, %2\n"
		"mov r5, %2\n"
		"mov r6, %2\n"
		"mov r7, %2\n"
		"mov r8, %2\n"
		"mov r9, %2\n"
		"mov r10, %2\n"
		"1:\n"
		"stmia %0!, {r3-r10}\n"
		"subs %1, %1, #8\n"
		"bgt 1b\n"
		: "+r" (dst), "+r" (words)
		: "r" (value)
		: "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "cc", "memory");
#else
	uint32_t* word = static_cast<uint32_t*>(dst);
	for (int i = 0; i < words; i++) word[i] = value;
#endif

}
