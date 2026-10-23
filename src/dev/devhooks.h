
/**
 *
 * @file devhooks.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Hooks in the engine's loaders for the developer tools (OJ_DEV_HOOKS).
 *
 */


#ifndef _DEVHOOKS_H
#define _DEVHOOKS_H

#include "io/bakepack.h"

#include <stdint.h>

/// A game data file was opened from disk for reading
void devOpened (const char* path, int size);

/// A loader decoded a block of a file
void devDecoded (const char* path, int offset, int holeStart, int end,
	BakeKind kind, int param, const unsigned char* data, int length);

/// A loader read a raw block of a file
void devRead (const unsigned char* data, int length);

/// A loader read bytes of a file directly, rather than through a decoder
void devRawRead (const char* path, int offset, int length);

/// Brackets a decode, so that the decoder's own reads are not raw reads
void devDecoding (bool start);

/// Pauses the hooks, for decodes that the caller reports as a whole instead.
/// Reads made meanwhile count as part of that decode.
void devSuspend (bool suspend);

/// Hash of every block loaded since the last reset, for checking that two
/// ways of loading the same item give the loaders the same data
uint64_t devDigest ();
void     devDigestReset ();

#endif
