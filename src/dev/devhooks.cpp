
/**
 *
 * @file devhooks.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Hooks in the engine's loaders for the developer tools. Keeps a digest of
 * the loaded data and passes decoded blocks on to the bake tool.
 *
 */


#include "devhooks.h"

#if OJ_BAKE
	#include "bake.h"
#endif

namespace {

	bool suspended = false;
	int decoding = 0;
	uint64_t digest = 0;

	void hash (const unsigned char* data, int length) {

		// FNV-1a, folding in the length so empty blocks still count
		uint64_t h = digest ^ static_cast<uint32_t>(length);

		for (int i = 0; i < length; i++) {
			h ^= data[i];
			h *= 0x100000001b3ULL;
		}

		digest = h * 0x100000001b3ULL;

	}

}


void devOpened (const char* path, int size) {

#if OJ_BAKE
	bakeOpened(path, size);
#else
	(void)path;
	(void)size;
#endif

}


void devDecoded (const char* path, int offset, int holeStart, int end,
	BakeKind kind, int param, const unsigned char* data, int length) {

	if (suspended) return;

	hash(data, length);

#if OJ_BAKE
	bakeRecord(path, offset, holeStart, end, kind, param, data, length);
#else
	(void)path;
	(void)offset;
	(void)holeStart;
	(void)end;
	(void)kind;
	(void)param;
#endif

}


void devRead (const unsigned char* data, int length) {

	if (!suspended) hash(data, length);

}


void devRawRead (const char* path, int offset, int length) {

	// While suspended, the caller reports the whole block as one decode
	if (decoding || suspended) return;

#if OJ_BAKE
	bakeRawRead(path, offset, length);
#else
	(void)path;
	(void)offset;
	(void)length;
#endif

}


void devDecoding (bool start) {

	decoding += start ? 1 : -1;

}


void devSuspend (bool suspend) {

	suspended = suspend;

}


uint64_t devDigest () {

	return digest;

}


void devDigestReset () {

	digest = 0;

}
