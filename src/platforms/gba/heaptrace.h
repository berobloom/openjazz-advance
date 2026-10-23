
/**
 *
 * @file heaptrace.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Heap tracing for finding where the RAM goes (GBA_HEAP_TRACE).
 *
 */


#ifndef OJ_GBA_HEAPTRACE_H
#define OJ_GBA_HEAPTRACE_H

/// Credit a traced block to the given code instead of its direct caller
void gbaHeapTraceCaller (void* ptr, void* caller);

/// Print the live heap blocks, grouped by the code that allocated them
void gbaHeapTraceReport ();

#endif
