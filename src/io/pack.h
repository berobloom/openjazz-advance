
/**
 *
 * @file pack.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _PACK_H
#define _PACK_H

#include "io/bakepack.h"

bool                 packActive      ();
const BakePackFile*  packFindFile    (const char* name);
const unsigned char* packSpan        (const BakePackFile* file, uint32_t pos, uint32_t& start, uint32_t& end);
const BakePackEntry* packFindDecoded (const BakePackFile* file, int offset, BakeKind kind, int length, int param);
const unsigned char* packData        (uint32_t offset);
const unsigned char* packFindStored  (const BakePackFile* file, int offset, int length);
bool                 packContains    (const void* pointer);

void                 packNoteMissing (const char* name);
int                  packHoleReads   ();
void                 packReport      ();

#endif
