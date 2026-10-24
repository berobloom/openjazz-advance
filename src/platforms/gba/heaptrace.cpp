
/**
 *
 * @file heaptrace.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Records every live heap block with the code that allocated it, for
 * finding where the RAM goes (GBA_HEAP_TRACE). The linker sends malloc,
 * free, realloc and calloc here. The records take 16KB of EWRAM, so a
 * traced build runs out of RAM sooner. (Sprite VRAM would be free on the
 * bitmap screen, but levels use it for their sprites.)
 *
 */


#include "heaptrace.h"

#include <gba_base.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

extern "C" {
	void* __real_malloc (size_t size);
	void  __real_free (void* ptr);
	void* __real_realloc (void* ptr, size_t size);
	void* __real_calloc (size_t n, size_t size);
}

namespace {

	struct Record {
		void*        ptr;
		unsigned int size;
		void*        caller;
	};

	const int maxRecords = 0x4000 / sizeof(Record);
	EWRAM_BSS Record records[maxRecords];
	int nRecords = 0;
	int lostRecords = 0;

	void add (void* ptr, size_t size, void* caller) {
		if (!ptr) return;

		if (nRecords == maxRecords) {
			lostRecords++;
			return;
		}

		records[nRecords].ptr = ptr;
		records[nRecords].size = size;
		records[nRecords].caller = caller;
		nRecords++;
	}

	void remove (void* ptr) {
		if (!ptr) return;

		for (int i = nRecords - 1; i >= 0; i--) {
			if (records[i].ptr == ptr) {
				nRecords--;
				records[i].ptr = records[nRecords].ptr;
				records[i].size = records[nRecords].size;
				records[i].caller = records[nRecords].caller;
				return;
			}
		}
	}

}


extern "C" void* __wrap_malloc (size_t size) {
	void* ptr = __real_malloc(size);
	add(ptr, size, __builtin_return_address(0));

	return ptr;
}

extern "C" void __wrap_free (void* ptr) {
	remove(ptr);
	__real_free(ptr);
}

extern "C" void* __wrap_realloc (void* ptr, size_t size) {
	void* result = __real_realloc(ptr, size);

	if (result || !size) {
		remove(ptr);
		add(result, size, __builtin_return_address(0));
	}

	return result;
}

extern "C" void* __wrap_calloc (size_t n, size_t size) {
	void* ptr = __real_calloc(n, size);
	add(ptr, n * size, __builtin_return_address(0));

	return ptr;
}


void gbaHeapTraceCaller (void* ptr, void* caller) {
	for (int i = nRecords - 1; i >= 0; i--) {
		if (records[i].ptr == ptr) {
			records[i].caller = caller;
			return;
		}
	}
}


void gbaHeapTraceReport () {
	const int maxCallers = 48;
	struct {
		void*        caller;
		unsigned int bytes;
		int          blocks;
	} callers[maxCallers];
	int nCallers = 0;
	unsigned int other = 0;
	unsigned int traced = 0;

	for (int i = 0; i < nRecords; i++) {
		traced += records[i].size;

		int c = 0;
		while (c < nCallers && callers[c].caller != records[i].caller) c++;

		if (c == nCallers) {
			if (nCallers == maxCallers) {
				other += records[i].size;
				continue;
			}
			callers[c].caller = records[i].caller;
			callers[c].bytes = 0;
			callers[c].blocks = 0;
			nCallers++;
		}

		callers[c].bytes += records[i].size;
		callers[c].blocks++;
	}

	// Largest first
	for (int i = 1; i < nCallers; i++) {
		for (int j = i; j > 0 && callers[j].bytes > callers[j - 1].bytes; j--) {
			auto swap = callers[j];
			callers[j] = callers[j - 1];
			callers[j - 1] = swap;
		}
	}

	struct mallinfo info = mallinfo();
	fprintf(stderr, "heap: %d bytes in use, %u traced in %d blocks, %d blocks not recorded\n",
		info.uordblks, traced, nRecords, lostRecords);
	for (int i = 0; i < nCallers; i++)
		fprintf(stderr, "heap: %7u bytes %4d blocks from %p\n", callers[i].bytes, callers[i].blocks, callers[i].caller);
	if (other) fprintf(stderr, "heap: %7u bytes from other callers\n", other);

	for (int i = 0; i < nRecords; i++) {
		if (records[i].size >= 4096)
			fprintf(stderr, "heap: block of %u bytes from %p\n", records[i].size, records[i].caller);
	}
}
