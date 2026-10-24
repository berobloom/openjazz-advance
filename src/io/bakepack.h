
/**
 *
 * @file bakepack.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Layout of the baked game data pack. The pack holds the game files that the
 * engine opens, plus the results of decoding their graphics ahead of time, so
 * that a port with little RAM can use decoded data in place from ROM. The
 * compressed bytes behind each decoded entry are left out of the stored file
 * ("holes"); reading them is never needed, as the entry is used instead.
 *
 * All values are little-endian and every table starts on a 4-byte boundary.
 *
 */


#ifndef _BAKEPACK_H
#define _BAKEPACK_H

#include <stdint.h>

#define BAKEPACK_MAGIC   "OJPK"
#define BAKEPACK_VERSION 1

/// How an entry's data was decoded from its file
enum BakeKind {
	BAKE_RLE = 1,           ///< File::loadRLE()
	BAKE_PIXELS = 2,        ///< File::loadPixels(length)
	BAKE_MASKED_PIXELS = 3, ///< File::loadPixels(length, key); param is the key
	BAKE_TILESET = 4,       ///< JJ1Level::loadTiles(), all tiles of a BLOCKS file
	BAKE_FONT_ATLAS = 5,    ///< A Font's character atlas; param tells fonts of one file apart
	BAKE_SCENE = 6          ///< A cutscene image; param is a SceneBake
};

/// What a BAKE_SCENE entry holds
enum SceneBake {
	SCENE_BAKE_BACKGROUND = 0, ///< An animation's compacted background, decoded
	SCENE_BAKE_RLE_IMAGE = 1   ///< An RLE image's pixels, decoded
};

struct BakePackHeader {
	char     magic[4];      ///< BAKEPACK_MAGIC
	uint32_t version;       ///< BAKEPACK_VERSION
	uint32_t nFiles;
	uint32_t filesOffset;   ///< BakePackFile[nFiles], sorted by name
	uint32_t nEntries;
	uint32_t entriesOffset; ///< BakePackEntry[nEntries], sorted by file, offset, kind, length, param
	uint32_t nSegments;
	uint32_t segmentsOffset; ///< BakePackSegment[nSegments]
	uint32_t size;          ///< Size of the whole pack
};

struct BakePackFile {
	char     name[16];      ///< Upper case, NUL-terminated
	uint32_t size;          ///< Size of the original file
	uint32_t firstSegment;  ///< Index of the file's first segment
	uint32_t nSegments;     ///< Stored parts of the file, in order; gaps between them are holes
};

struct BakePackSegment {
	uint32_t offset;        ///< Position in the original file
	uint32_t length;
	uint32_t dataOffset;    ///< Position in the pack
};

struct BakePackEntry {
	uint16_t file;          ///< Index into the file table
	uint8_t  kind;          ///< BakeKind
	uint8_t  param;         ///< Depends on kind
	uint32_t offset;        ///< File position when the decode started
	uint32_t end;           ///< File position when the decode finished
	uint32_t length;        ///< Decoded length
	uint32_t dataOffset;    ///< Position of the decoded data in the pack
};

#endif
