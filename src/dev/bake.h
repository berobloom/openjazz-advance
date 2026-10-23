
/**
 *
 * @file bake.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _BAKE_H
#define _BAKE_H

#include "io/bakepack.h"

void bakeOpened (const char* path, int size);
void bakeRecord (const char* path, int offset, int holeStart, int end,
	BakeKind kind, int param, const unsigned char* data, int length);
void bakeRawRead (const char* path, int offset, int length);
int  bakeSweep  (const char* gameDir, const char* outPath);

#endif
