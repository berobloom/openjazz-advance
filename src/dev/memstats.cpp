
/**
 *
 * @file memstats.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Developer tool for sizing the Game Boy Advance port. Tracks heap usage by
 * replacing the C allocator, then loads everything in the game directory in
 * turn (see sweep.cpp) and reports how much memory each item needs.
 * Linux/glibc only.
 *
 */


#include "memstats.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <execinfo.h>
#include <malloc.h>
#include <vector>

#include "sweep.h"

long soundMemory ();

/* Heap accounting */

extern "C" {
	void* __libc_malloc(size_t size);
	void* __libc_calloc(size_t n, size_t size);
	void* __libc_realloc(void* ptr, size_t size);
	void* __libc_memalign(size_t alignment, size_t size);
	void  __libc_free(void* ptr);
}

static std::atomic<long> heapCurrent(0);
static std::atomic<long> heapPeak(0);

// Live allocations, recorded while a detailed report is wanted
#define DETAIL_SLOTS 65536
#define DETAIL_FRAMES 8
static struct { void* ptr; size_t size; void* frames[DETAIL_FRAMES]; int nFrames; } detail[DETAIL_SLOTS];
static std::atomic<bool> detailOn(false);
static size_t detailMinimum = 8192;

static void* track (void* ptr) {

	if (ptr) {
		if (detailOn) {
			for (int i = 0; i < DETAIL_SLOTS; i++) {
				if (!detail[i].ptr) {
					detail[i].ptr = ptr;
					detail[i].size = malloc_usable_size(ptr);
					detail[i].nFrames = detail[i].size >= detailMinimum ?
						backtrace(detail[i].frames, DETAIL_FRAMES) : 0;
					break;
				}
			}
		}
		long now = heapCurrent += malloc_usable_size(ptr);
		long peak = heapPeak;
		while (now > peak && !heapPeak.compare_exchange_weak(peak, now)) {}
	}

	return ptr;

}

static void untrack (void* ptr) {

	if (ptr) {
		if (detailOn) {
			for (int i = 0; i < DETAIL_SLOTS; i++) {
				if (detail[i].ptr == ptr) {
					detail[i].ptr = nullptr;
					break;
				}
			}
		}
		heapCurrent -= malloc_usable_size(ptr);
	}

}

extern "C" {

void* malloc (size_t size) {
	return track(__libc_malloc(size));
}

void* calloc (size_t n, size_t size) {
	return track(__libc_calloc(n, size));
}

void* realloc (void* ptr, size_t size) {
	untrack(ptr);
	void* result = __libc_realloc(ptr, size);
	// on failure the old block is still allocated
	return track(result ? result : (size ? ptr : nullptr));
}

void* memalign (size_t alignment, size_t size) {
	return track(__libc_memalign(alignment, size));
}

void* aligned_alloc (size_t alignment, size_t size) {
	return track(__libc_memalign(alignment, size));
}

int posix_memalign (void** ptr, size_t alignment, size_t size) {
	*ptr = track(__libc_memalign(alignment, size));
	return *ptr ? 0 : 12; // ENOMEM
}

void free (void* ptr) {
	untrack(ptr);
	__libc_free(ptr);
}

}

/* Sweep */

static long baseline;       ///< Heap held by the engine before the sweep
static long baselineSound;  ///< Part of baseline held by sound effects

struct Sample {
	char name[16];
	long steady;  ///< Heap held while loaded, above the engine baseline
	long peak;    ///< Highest heap during loading, above the engine baseline
	long sound;   ///< Part of steady held by resampled sound effects
	bool failed;
};

static void reportDetail (const char* name) {

	std::vector<size_t> sizes;
	long small = 0;
	int nSmall = 0;

	for (int i = 0; i < DETAIL_SLOTS; i++) {
		if (!detail[i].ptr) continue;
		if (detail[i].size >= 1024) sizes.push_back(detail[i].size);
		else { small += detail[i].size; nSmall++; }
	}

	std::sort(sizes.begin(), sizes.end(), std::greater<size_t>());

	printf("\nCallers of live allocations of %zu bytes or more made while loading %s\n", detailMinimum, name);
	fflush(stdout);
	for (int i = 0; i < DETAIL_SLOTS; i++) {
		if (!detail[i].ptr || !detail[i].nFrames) continue;
		printf("== %zu\n", detail[i].size);
		fflush(stdout);
		backtrace_symbols_fd(detail[i].frames, detail[i].nFrames, 1);
	}

	printf("\nLive allocations made while loading %s (%zu of 1K or more)\n", name, sizes.size());
	for (size_t i = 0; i < sizes.size(); i++) printf("%zu%s", sizes[i], (i % 10 == 9) ? "\n" : " ");
	printf("\nplus %d allocations under 1K totalling %ld\n", nSmall, small);

}

static Sample measure (const char* name, const SweepLoad& load) {

	const char* detailName = getenv("OJ_MEMSTATS_DETAIL");
	bool wantDetail = detailName && strcasecmp(detailName, name) == 0;

	if (wantDetail) {
		if (const char* minimum = getenv("OJ_MEMSTATS_TRACE_MIN")) detailMinimum = atol(minimum);
		void* warmup[1];
		backtrace(warmup, 1); // loads the unwinder before tracking starts
		memset(detail, 0, sizeof(detail));
		detailOn = true;
	}

	Sample sample;
	snprintf(sample.name, sizeof(sample.name), "%s", name);

	heapPeak.store(heapCurrent);

	sample.failed = false;
	sample.steady = 0;
	sample.sound = 0;

	try {
		auto unload = load();
		sample.steady = heapCurrent - baseline;
		sample.peak = heapPeak - baseline;
		sample.sound = soundMemory();
		if (wantDetail) {
			detailOn = false;
			reportDetail(name);
		}
		unload();
	} catch (int e) {
		sample.failed = true;
		sample.peak = heapPeak - baseline;
	}

	return sample;

}

static void report (const char* title, std::vector<Sample>& samples) {

	long maxSteady = 0, maxPeak = 0, noSound;
	const char *maxSteadyName = "-", *maxPeakName = "-";

	printf("\n%s\n%-14s %10s %10s %10s %10s\n", title, "file", "steady", "peak", "sound", "no-sound");

	for (Sample& s : samples) {
		noSound = s.steady - s.sound + baselineSound;
		printf("%-14s %10ld %10ld %10ld %10ld%s\n", s.name, s.steady, s.peak, s.sound,
			noSound, s.failed ? "  (failed to load)" : "");
		if (s.failed) continue;
		if (noSound > maxSteady) { maxSteady = noSound; maxSteadyName = s.name; }
		if (s.peak > maxPeak) { maxPeak = s.peak; maxPeakName = s.name; }
	}

	printf("largest no-sound: %ld (%s), largest peak: %ld (%s)\n",
		maxSteady, maxSteadyName, maxPeak, maxPeakName);

}

int memstatsSweep (const char* gameDir) {

	baseline = heapCurrent;
	baselineSound = soundMemory();
	printf("memstats: engine baseline heap %ld, of which sound %ld\n", baseline, baselineSound);

	std::vector<Sample> samples[SWEEP_TYPES];

	bool ok = sweepGameData(gameDir, [&](const char* name, SweepType type, const SweepLoad& load) {
		samples[type].push_back(measure(name, load));
	});

	if (!ok) return 1;

	for (int type = 0; type < SWEEP_TYPES; type++)
		report(sweepTypeNames[type], samples[type]);

	return 0;

}
