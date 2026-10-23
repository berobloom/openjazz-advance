
/**
 *
 * @file pack.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Reads the baked game data pack (see bakepack.h). Files in the pack are
 * opened as stdio streams, and the loaders use decoded blocks and stored file
 * data in place, so the pack must stay mapped for the life of the program.
 *
 * Buffers handed out from the pack are read-only. The loaders free their
 * buffers with delete[], so delete ignores pointers into the pack. Surfaces
 * made from pack pixels use them in place (see Video::createSurface).
 *
 * On the desktop the pack is the file named by the OJ_PACK environment
 * variable, mapped read-only so that any write to it faults.
 *
 */


#include "pack.h"
#include "io/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <strings.h>

#ifndef __NEWLIB__
	#include <fcntl.h>
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

#ifdef __NEWLIB__
	typedef _off64_t pack_off_t;
#else
	typedef off64_t pack_off_t;
#endif

namespace {

	const unsigned char* pack = nullptr;
	uint32_t packSize = 0;
	bool loaded = false;

	const BakePackHeader* header = nullptr;
	const BakePackFile* files = nullptr;
	const BakePackEntry* entries = nullptr;
	const BakePackSegment* segments = nullptr;

	int holeReads = 0;
	std::set<std::string>* missing = nullptr;

	/// An open file in the pack
	struct Stream {
		const BakePackFile* file;
		uint32_t pos;
	};

	bool load () {

#ifndef __NEWLIB__
		const char* path = getenv("OJ_PACK");
		if (!path) return false;

		int fd = open(path, O_RDONLY);
		struct stat st;
		if (fd < 0 || fstat(fd, &st) != 0) {
			LOG_ERROR("Could not open pack %s", path);
			if (fd >= 0) close(fd);
			return false;
		}

		void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (map == MAP_FAILED) {
			LOG_ERROR("Could not map pack %s", path);
			return false;
		}

		pack = static_cast<const unsigned char*>(map);
		packSize = st.st_size;
#else
		return false;
#endif

		header = reinterpret_cast<const BakePackHeader*>(pack);
		if (packSize < sizeof(BakePackHeader) || memcmp(header->magic, BAKEPACK_MAGIC, 4) != 0 ||
			header->version != BAKEPACK_VERSION || header->size != packSize) {
			LOG_ERROR("Game data pack is not valid");
			pack = nullptr;
			return false;
		}

		files = reinterpret_cast<const BakePackFile*>(pack + header->filesOffset);
		entries = reinterpret_cast<const BakePackEntry*>(pack + header->entriesOffset);
		segments = reinterpret_cast<const BakePackSegment*>(pack + header->segmentsOffset);

		LOG_INFO("Using game data pack: %u files, %u decoded blocks", header->nFiles, header->nEntries);

		return true;

	}

	ssize_t streamRead (void* cookie, char* buffer, size_t size) {

		Stream* stream = static_cast<Stream*>(cookie);
		const BakePackFile* file = stream->file;

		if (stream->pos >= file->size) return 0;
		if (size > file->size - stream->pos) size = file->size - stream->pos;

		uint32_t pos = stream->pos;
		const BakePackSegment* segment = segments + file->firstSegment;
		const BakePackSegment* last = segment + file->nSegments;

		// Find the first segment that ends after pos
		while (segment < last && segment->offset + segment->length <= pos) segment++;

		/* Stop at the end of the segment rather than reading on into a hole.
		   stdio reads ahead to fill its buffer, and only reaches a hole this
		   way when a loader really asks for bytes in it. */
		size_t count;

		if (segment < last && segment->offset <= pos) {
			count = std::min<size_t>(size, segment->offset + segment->length - pos);
			memcpy(buffer, pack + segment->dataOffset + (pos - segment->offset), count);
		} else {
			// These bytes were replaced by a decoded block, so should not be needed
			uint32_t holeEnd = segment < last ? segment->offset : file->size;
			count = std::min<size_t>(size, holeEnd - pos);
			memset(buffer, 0, count);
			if (!holeReads++) LOG_WARN("Read from a hole in %s at %u", file->name, pos);
		}

		stream->pos += count;

		return count;

	}

	int streamSeek (void* cookie, pack_off_t* offset, int whence) {

		Stream* stream = static_cast<Stream*>(cookie);
		pack_off_t pos;

		switch (whence) {
			case SEEK_SET: pos = *offset; break;
			case SEEK_CUR: pos = stream->pos + *offset; break;
			case SEEK_END: pos = stream->file->size + *offset; break;
			default: return -1;
		}

		if (pos < 0) return -1;

		stream->pos = pos;
		*offset = pos;

		return 0;

	}

	int streamClose (void* cookie) {

		delete static_cast<Stream*>(cookie);

		return 0;

	}

}


/**
 * Whether there is a game data pack to load files from.
 */
bool packActive () {

	if (!loaded) {
		loaded = true;
		load();
	}

	return pack != nullptr;

}


/**
 * Find a file in the pack.
 *
 * @param name File name, matched case-insensitively
 *
 * @return The file, or nullptr if the pack does not have it
 */
const BakePackFile* packFindFile (const char* name) {

	if (!packActive()) return nullptr;

	const BakePackFile* end = files + header->nFiles;
	const BakePackFile* found = std::lower_bound(files, end, name,
		[](const BakePackFile& file, const char* key) { return strcasecmp(file.name, key) < 0; });

	if (found == end || strcasecmp(found->name, name) != 0) return nullptr;

	return found;

}


/**
 * Open a file in the pack for reading.
 */
FILE* packOpen (const BakePackFile* file) {

	cookie_io_functions_t functions = {streamRead, nullptr, streamSeek, streamClose};

	FILE* stream = fopencookie(new Stream{file, 0}, "rb", functions);

#ifndef __NEWLIB__
	/* glibc refills its buffer from a block boundary after seeking, reading
	   bytes nobody asked for. Unbuffered, every read is a loader's own, so
	   the count of reads from holes is exact. */
	if (stream) setvbuf(stream, nullptr, _IONBF, 0);
#endif

	return stream;

}


/**
 * Find the decoded block that a loader would produce.
 *
 * @param file The file being loaded from
 * @param offset File position at which the loader starts decoding
 * @param kind How the loader decodes
 * @param length Decoded length, or -1 for any
 * @param param Depends on kind
 *
 * @return The entry, or nullptr if the block was not baked
 */
const BakePackEntry* packFindDecoded (const BakePackFile* file, int offset, BakeKind kind, int length, int param) {

	uint16_t index = file - files;
	const BakePackEntry* end = entries + header->nEntries;

	// Entries are sorted by file, offset, kind, length and param
	const BakePackEntry* entry = std::lower_bound(entries, end, file,
		[index, offset, kind](const BakePackEntry& e, const BakePackFile*) {
			if (e.file != index) return e.file < index;
			if (e.offset != static_cast<uint32_t>(offset)) return e.offset < static_cast<uint32_t>(offset);
			return e.kind < kind;
		});

	for (; entry < end && entry->file == index && entry->offset == static_cast<uint32_t>(offset) &&
		entry->kind == kind; entry++) {
		if ((length < 0 || entry->length == static_cast<uint32_t>(length)) && entry->param == param)
			return entry;
	}

	return nullptr;

}


const unsigned char* packData (uint32_t offset) {

	return pack + offset;

}


/**
 * Find a run of a file's bytes that the pack stores in one piece.
 *
 * @return The bytes, or nullptr if they cross a hole or a segment boundary
 */
const unsigned char* packFindStored (const BakePackFile* file, int offset, int length) {

	const BakePackSegment* segment = segments + file->firstSegment;
	const BakePackSegment* last = segment + file->nSegments;

	for (; segment < last; segment++) {
		if (static_cast<uint32_t>(offset) >= segment->offset &&
			static_cast<uint32_t>(offset + length) <= segment->offset + segment->length)
			return pack + segment->dataOffset + (offset - segment->offset);
	}

	return nullptr;

}


bool packContains (const void* pointer) {

	const unsigned char* p = static_cast<const unsigned char*>(pointer);

	return pack && p >= pack && p < pack + packSize;

}


/**
 * Note a game data file that had to be opened from outside the pack.
 */
void packNoteMissing (const char* name) {

	if (!missing) missing = new std::set<std::string>;

	if (missing->insert(name).second) LOG_DEBUG("%s is not in the game data pack", name);

}


int packHoleReads () {

	return holeReads;

}


void packReport () {

	if (!packActive()) return;

	printf("pack: %d reads from holes, %zu game files opened from outside the pack:",
		holeReads, missing ? missing->size() : 0);
	if (missing) for (const std::string& name : *missing) printf(" %s", name.c_str());
	printf("\n");

}


/* The loaders free their buffers with delete[], including the ones that come
   from the pack. */

void operator delete (void* pointer) noexcept {

	if (!packContains(pointer)) free(pointer);

}

void operator delete[] (void* pointer) noexcept {

	if (!packContains(pointer)) free(pointer);

}

void operator delete (void* pointer, size_t) noexcept {

	if (!packContains(pointer)) free(pointer);

}

void operator delete[] (void* pointer, size_t) noexcept {

	if (!packContains(pointer)) free(pointer);

}
