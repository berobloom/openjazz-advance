
/**
 *
 * @file sram_gba.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Keeps the files the game writes (the settings and saved games) in the
 * cartridge's 32 KB of battery-backed SRAM. The SRAM is the default stdio
 * device, so fopen() reaches it with the engine's plain file names.
 *
 * Each file has a fixed slot. A file's data is written first, and its size
 * and checksum last, so a file cut short by switching off reads as missing,
 * not as rubbish. SRAM takes only byte accesses, so everything goes through
 * byte loops.
 *
 */


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/iosupport.h>
#include <sys/stat.h>

namespace {

	volatile unsigned char* const sram = reinterpret_cast<volatile unsigned char*>(0x0E000000);

	const char MAGIC[4] = {'O', 'J', 'S', 'V'};
	const int VERSION = 1;

	const int MAX_FILES = 8;
	const int NAME_LENGTH = 24; ///< Including the terminator
	const int DIRECTORY = 0x10; ///< Entries: name, size (2 bytes), checksum (2 bytes), spare (4 bytes)
	const int ENTRY_SIZE = 32;
	const int SLOTS = 0x200;
	const int SLOT_SIZE = 0xE00;

	/// The emulators and flash carts look for this to give the game SRAM
	__attribute__((aligned(4), used)) const char saveType[] = "SRAM_V113";

	struct SramFile {
		int  entry;
		int  pos;
		int  size;
		bool writing;
	};

	int entryAt (int entry) {
		return DIRECTORY + (entry * ENTRY_SIZE);
	}

	int slotAt (int entry) {
		return SLOTS + (entry * SLOT_SIZE);
	}

	int load16 (int at) {
		return sram[at] | (sram[at + 1] << 8);
	}

	void store16 (int at, int value) {
		sram[at] = value & 255;
		sram[at + 1] = value >> 8;
	}

	/// Fletcher-16 of a slot's data
	int checksum (int entry, int size) {
		int a = 0, b = 0;
		int at = slotAt(entry);

		for (int i = 0; i < size; i++) {
			a = (a + sram[at + i]) % 255;
			b = (b + a) % 255;
		}

		return (b << 8) | a;
	}

	bool nameIs (int entry, const char* name) {
		int at = entryAt(entry);

		for (int i = 0; i < NAME_LENGTH; i++) {
			char c = sram[at + i];
			if (tolower(c) != tolower(name[i])) return false;
			if (!c) return true;
		}

		return false;
	}

	bool used (int entry) {
		return sram[entryAt(entry)] != 0;
	}

	/// Size of a file whose data matches its checksum, or -1
	int validSize (int entry) {
		int at = entryAt(entry);
		int size = load16(at + NAME_LENGTH);

		if (size > SLOT_SIZE || checksum(entry, size) != load16(at + NAME_LENGTH + 2)) return -1;

		return size;
	}

	/// The file name without a device or leading slashes
	const char* baseName (const char* path) {
		const char* colon = strchr(path, ':');
		if (colon) path = colon + 1;
		while (*path == '/') path++;

		return path;
	}

	int find (const char* name) {
		for (int i = 0; i < MAX_FILES; i++)
			if (used(i) && nameIs(i, name)) return i;

		return -1;
	}

	int sramOpen (struct _reent* r, void* fileStruct, const char* path, int flags, int mode) {
		(void)mode;

		SramFile* file = static_cast<SramFile*>(fileStruct);
		const char* name = baseName(path);
		int entry = find(name);

		if ((flags & O_ACCMODE) == O_RDONLY) {
			int size = entry < 0? -1: validSize(entry);
			if (size < 0) {
				r->_errno = ENOENT;
				return -1;
			}

			*file = {entry, 0, size, false};
			return 0;
		}

		if (static_cast<int>(strlen(name)) >= NAME_LENGTH) {
			r->_errno = ENAMETOOLONG;
			return -1;
		}

		if (entry < 0) {
			for (int i = 0; i < MAX_FILES && entry < 0; i++)
				if (!used(i)) entry = i;

			if (entry < 0) {
				r->_errno = ENOSPC;
				return -1;
			}
		}

		// Name the entry now, with a checksum that the empty file fails until it is closed
		int at = entryAt(entry);
		store16(at + NAME_LENGTH, 0);
		store16(at + NAME_LENGTH + 2, ~checksum(entry, 0) & 0xFFFF);
		for (int i = 0; i < NAME_LENGTH; i++) {
			sram[at + i] = name[i];
			if (!name[i]) break;
		}

		*file = {entry, 0, 0, true};
		return 0;
	}

	int sramClose (struct _reent* r, void* fd) {
		(void)r;

		SramFile* file = static_cast<SramFile*>(fd);

		if (file->writing) {
			int at = entryAt(file->entry);
			store16(at + NAME_LENGTH + 2, checksum(file->entry, file->size));
			store16(at + NAME_LENGTH, file->size);
		}

		return 0;
	}

	ssize_t sramWrite (struct _reent* r, void* fd, const char* ptr, size_t len) {
		SramFile* file = static_cast<SramFile*>(fd);

		if (!file->writing) {
			r->_errno = EBADF;
			return -1;
		}

		int count = static_cast<int>(len);
		if (file->pos + count > SLOT_SIZE) count = SLOT_SIZE - file->pos;
		if (count <= 0) {
			r->_errno = ENOSPC;
			return -1;
		}

		int at = slotAt(file->entry) + file->pos;
		for (int i = 0; i < count; i++) sram[at + i] = ptr[i];

		file->pos += count;
		if (file->pos > file->size) file->size = file->pos;

		return count;
	}

	ssize_t sramRead (struct _reent* r, void* fd, char* ptr, size_t len) {
		(void)r;

		SramFile* file = static_cast<SramFile*>(fd);

		int count = static_cast<int>(len);
		if (file->pos + count > file->size) count = file->size - file->pos;
		if (count <= 0) return 0;

		int at = slotAt(file->entry) + file->pos;
		for (int i = 0; i < count; i++) ptr[i] = sram[at + i];

		file->pos += count;

		return count;
	}

	off_t sramSeek (struct _reent* r, void* fd, off_t pos, int dir) {
		SramFile* file = static_cast<SramFile*>(fd);

		int base = dir == SEEK_CUR? file->pos: (dir == SEEK_END? file->size: 0);
		int to = base + pos;
		if (to < 0 || to > (file->writing? SLOT_SIZE: file->size)) {
			r->_errno = EINVAL;
			return -1;
		}

		file->pos = to;
		return to;
	}

	int sramFstat (struct _reent* r, void* fd, struct stat* st) {
		(void)r;

		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
		st->st_size = static_cast<SramFile*>(fd)->size;

		return 0;
	}

	int sramStat (struct _reent* r, const char* path, struct stat* st) {
		const char* name = baseName(path);

		// The directory everything is in
		if (!*name) {
			memset(st, 0, sizeof(*st));
			st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
			return 0;
		}

		int entry = find(name);
		int size = entry < 0? -1: validSize(entry);

		if (size < 0) {
			r->_errno = ENOENT;
			return -1;
		}

		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
		st->st_size = size;

		return 0;
	}

	// Constant, so it stays in ROM, with no links, renaming or directories
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	const devoptab_t sramDevice = {
		"sram", sizeof(SramFile), sramOpen, sramClose, sramWrite, sramRead, sramSeek, sramFstat, sramStat
	};
#pragma GCC diagnostic pop

	__attribute__((constructor)) void initSram () {
		// Keep the save type marker in the ROM
		asm volatile ("" :: "r" (saveType));

		bool formatted = true;
		for (int i = 0; i < 4; i++) if (sram[i] != MAGIC[i]) formatted = false;
		if (!formatted || sram[4] != VERSION) {
			for (int i = 0; i < MAX_FILES; i++) sram[entryAt(i)] = 0;
			sram[4] = VERSION;
			for (int i = 0; i < 4; i++) sram[i] = MAGIC[i];
		}

		int device = AddDevice(&sramDevice);
		if (device >= 0) setDefaultDevice(device);
	}

}
