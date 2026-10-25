
/**
 *
 * @file gba.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */

#include "gba.h"

#ifdef __GBA__

#include "io/log.h"

#ifdef OJ_GBA_HEAP_TRACE
	#include "gba/heaptrace.h"
#endif

#include <malloc.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/time.h>

#include <SDL.h>

#include <gba_systemcalls.h>

extern "C" char __iheap_start[]; // End of the data in IWRAM

namespace {

	// mGBA's debug output registers
	volatile unsigned short& debugEnable = *reinterpret_cast<volatile unsigned short*>(0x4FFF780);
	volatile unsigned short& debugFlags = *reinterpret_cast<volatile unsigned short*>(0x4FFF700);
	char* const debugString = reinterpret_cast<char*>(0x4FFF600);
	const int debugStringSize = 0x100;
	const unsigned short debugLevelInfo = 3;

	char line[debugStringSize];
	int lineLength = 0;

	void flushLine () {
		line[lineLength] = 0;
		memcpy(debugString, line, lineLength + 1);
		debugFlags = debugLevelInfo | 0x100;
		lineLength = 0;
	}

	/// Sends stdout and stderr to the emulator's log, one line per message
	ssize_t debugWrite (struct _reent* r, void* fd, const char* ptr, size_t len) {
		(void)r;
		(void)fd;

		for (size_t i = 0; i < len; i++) {
			if (ptr[i] == '\n') {
				flushLine();
			} else {
				line[lineLength++] = ptr[i];
				if (lineLength == debugStringSize - 1) flushLine();
			}
		}

		return len;
	}

	devoptab_t debugDevice;

	/* The stack grows down from the top of IWRAM towards the data that
	   libsysbase and the engine keep there, so the unused part is filled with
	   a pattern and the bottom of it watched. */
	const unsigned int stackFill = 0x57AC57AC;
	const int stackGuardWords = 64;

	unsigned int* stackBottom () {
		return reinterpret_cast<unsigned int*>((reinterpret_cast<unsigned int>(__iheap_start) + 3) & ~3);
	}


	__attribute__((constructor)) void initStackFill () {
		unsigned int* top = static_cast<unsigned int*>(__builtin_frame_address(0)) - 64;
		for (unsigned int* p = stackBottom(); p < top; p++) *p = stackFill;
	}

	__attribute__((constructor)) void initDebugOutput () {
		debugEnable = 0xC0DE;
		if (debugEnable != 0x1DEA) return; // Not running in mGBA

		debugDevice.name = "debug";
		debugDevice.write_r = debugWrite;
		devoptab_list[STD_OUT] = &debugDevice;
		devoptab_list[STD_ERR] = &debugDevice;
	}

}

/* There is one thread, so waking waiters on a condition variable has nothing to
   do. libsysbase's defaults fail instead, and libstdc++ uses these behind its
   guarded statics, which the ARMv4 fallback for atomics depends on: a failure
   there throws from a noexcept function, ending the program at the first
   caught exception. */
extern "C" int __syscall_cond_signal (_COND_T *cond) {
	(void)cond;
	return 0;
}

extern "C" int __syscall_cond_broadcast (_COND_T *cond) {
	(void)cond;
	return 0;
}

/* Without a clock, time() fails and returns -1, which localtime() takes for
   31 December 1969, so the menu shows its Christmas theme. Count from the
   epoch instead. */
extern "C" int __syscall_gettod_r (struct _reent *ptr, struct timeval *tp, struct timezone *tz) {
	(void)ptr;
	(void)tz;

	if (tp) {
		Uint32 ticks = SDL_GetTicks();
		tp->tv_sec = ticks / 1000;
		tp->tv_usec = (ticks % 1000) * 1000;
	}

	return 0;
}

/* Report allocations that do not fit, as they are the usual way the port
   runs out of RAM. */
namespace {

	void* allocate (size_t size, void* caller) {
		void* p = malloc(size ? size : 1);

		if (!p) {
			struct mallinfo info = mallinfo();
			fprintf(stderr, "Allocation of %u bytes from %p does not fit: %d bytes in use, %d free in heap\n",
				static_cast<unsigned int>(size), caller, info.uordblks, info.fordblks);
#ifdef OJ_GBA_HEAP_TRACE
			gbaHeapTraceReport();
#endif
			throw std::bad_alloc();
		}

#ifdef OJ_GBA_HEAP_TRACE
		gbaHeapTraceCaller(p, caller);
#endif

		return p;
	}

}

void* operator new (size_t size) {
	return allocate(size, __builtin_return_address(0));
}

void* operator new[] (size_t size) {
	return allocate(size, __builtin_return_address(0));
}

void gbaCheckStack () {
	unsigned int* guard = stackBottom();

	for (int i = 0; i < stackGuardWords; i++) {
		if (guard[i] != stackFill) {
			// The overflow may have hit the stdio device table, so bypass it
			const char message[] = "Stack overflow: the stack reached the IWRAM data.";
			memcpy(line, message, sizeof(message) - 1);
			lineLength = sizeof(message) - 1;
			flushLine();
			while (true) VBlankIntrWait();
		}
	}
}

int gbaFreeStack () {
	unsigned int* p = stackBottom();
	while (*p == stackFill) p++;

	return reinterpret_cast<char*>(p) - reinterpret_cast<char*>(stackBottom());
}

extern "C" char __sbss_start__[], __sbss_end__[];

void gbaBootStage (unsigned short color) {
	*reinterpret_cast<volatile unsigned short*>(0x5000000) = color; // Backdrop
	*reinterpret_cast<volatile unsigned short*>(0x4000000) = 0; // Mode 0, no layers
}


/* Cartridge ROM at 3/2 wait states with prefetch, SRAM at 8. Some flash
   cartridges can't keep up with the faster 3/1 and crash right after the
   switch, leaving a white screen. */
__attribute__((constructor(101))) static void initRomTiming () {
	gbaBootStage(0x001F);
	*reinterpret_cast<volatile unsigned short*>(0x4000204) = 0x4007;
	gbaBootStage(0x03FF);
}


/// Zero the globals kept in EWRAM (OJ_SLOW_DATA) before any constructor uses them
__attribute__((constructor(102))) static void clearSlowData () {
	memset(__sbss_start__, 0, __sbss_end__ - __sbss_start__);
}

GbaPlatform::GbaPlatform() {
}

void GbaPlatform::ErrorNoDatafiles() {
	LOG_FATAL("The ROM was built without game data.");
}

#endif
