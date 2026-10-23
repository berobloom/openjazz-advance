
/**
 *
 * @file bake.cpp
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Developer tool that builds the baked game data pack (see io/bakepack.h).
 * While the engine loads everything in the game directory (see sweep.cpp),
 * the File class reports each file it opens and each block it decodes. The
 * pack is then written from those records, with identical decoded blocks
 * stored once, and a report of where the space goes.
 *
 */


#include "bake.h"
#include "sweep.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "io/log.h"

namespace {

	struct OpenedFile {
		std::string path;
		int size;
	};

	struct Record {
		int offset, holeStart, end;
		BakeKind kind;
		int param;
		size_t blob; ///< Index into blobs
	};

	using RecordKey = std::tuple<std::string, int, int, int, int>; // name, offset, kind, length, param

	std::map<std::string, OpenedFile> opened;
	std::map<RecordKey, Record> records;
	std::vector<std::string> blobs;
	std::unordered_map<std::string, size_t> blobIndex;
	bool suspended = false;

	std::string baseName (const char* path) {

		const char* slash = strrchr(path, '/');
		std::string name = slash ? slash + 1 : path;

		for (char& c : name) c = toupper(static_cast<unsigned char>(c));

		return name;

	}

	/// Group files by name for the report, e.g. BLOCKS.005 -> BLOCKS, ENDE6.0SC -> *.0SC
	std::string category (const std::string& name) {

		size_t dot = name.find('.');
		std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);

		if (!ext.empty() && !isdigit(static_cast<unsigned char>(ext[ext.size() - 1])))
			return "*." + ext;
		if (ext.size() == 3 && ext[0] == '0' && isalpha(static_cast<unsigned char>(ext[1])))
			return "*." + ext; // .0SC, .0FN

		std::string stem = name.substr(0, dot);
		while (!stem.empty() && isdigit(static_cast<unsigned char>(stem.back()))) stem.pop_back();

		return stem;

	}

	void align (std::string& out) {

		while (out.size() & 3) out.push_back(0);

	}

	template <typename T>
	void append (std::string& out, const T& value) {

		out.append(reinterpret_cast<const char*>(&value), sizeof(value));

	}

}


void bakeSuspend (bool suspend) {

	suspended = suspend;

}


void bakeOpened (const char* path, int size) {

	std::string name = baseName(path);

	if (!opened.count(name)) opened[name] = {path, size};

}


void bakeRecord (const char* path, int offset, int holeStart, int end,
	BakeKind kind, int param, const unsigned char* data, int length) {

	if (suspended) return;

	std::string name = baseName(path);

	/* Level and bonus level data is copied out of the decoded buffer into the
	   level's own structures, so a decoded copy in ROM would only save RAM
	   briefly while loading, at several times the size of the original. */
	std::string group = category(name);
	if (group == "LEVEL" || group == "BONUSMAP") return;
	std::string bytes(reinterpret_cast<const char*>(data), length);
	RecordKey key(name, offset, kind, length, param);

	auto found = records.find(key);
	if (found != records.end()) {
		if (blobs[found->second.blob] != bytes)
			LOG_WARN("bake: %s at %d decodes differently on a second load", name.c_str(), offset);
		return;
	}

	auto blob = blobIndex.find(bytes);
	size_t index;

	if (blob == blobIndex.end()) {
		index = blobs.size();
		blobs.push_back(bytes);
		blobIndex[bytes] = index;
	} else {
		index = blob->second;
	}

	records[key] = {offset, holeStart, end, kind, param, index};

}


/**
 * Work out which parts of a file to store: everything except the compressed
 * bytes that a decoded entry replaces.
 */
static std::vector<std::pair<int, int>> storedParts (const std::string& name, int size) {

	std::vector<std::pair<int, int>> holes;

	for (auto& item : records) {
		if (std::get<0>(item.first) != name) continue;
		const Record& r = item.second;
		if (r.end > r.holeStart) holes.push_back({r.holeStart, r.end});
	}

	std::sort(holes.begin(), holes.end());

	std::vector<std::pair<int, int>> parts;
	int pos = 0;

	for (auto& hole : holes) {
		if (hole.first > pos) parts.push_back({pos, hole.first - pos});
		pos = std::max(pos, hole.second);
	}

	if (pos < size) parts.push_back({pos, size - pos});

	return parts;

}


static bool writePack (const char* outPath) {

	// Music is converted separately, so leave it out of the pack
	std::vector<std::string> names;
	for (auto& item : opened)
		if (category(item.first) != "*.PSM") names.push_back(item.first);

	std::map<std::string, int> fileIndex;
	for (size_t i = 0; i < names.size(); i++) fileIndex[names[i]] = i;

	struct CategoryStats {
		int files = 0, entries = 0;
		long original = 0, stored = 0, decoded = 0, unique = 0;
	};
	std::map<std::string, CategoryStats> stats;
	std::vector<bool> counted(blobs.size(), false);

	std::vector<BakePackFile> files;
	std::vector<BakePackSegment> segments;
	std::vector<BakePackEntry> entries;
	std::string data;

	// File contents

	for (std::string& name : names) {

		OpenedFile& file = opened[name];
		FILE* f = fopen(file.path.c_str(), "rb");
		if (!f) {
			LOG_ERROR("bake: cannot reopen %s", file.path.c_str());
			return false;
		}

		BakePackFile packFile = {};
		snprintf(packFile.name, sizeof(packFile.name), "%s", name.c_str());
		packFile.size = file.size;
		packFile.firstSegment = segments.size();

		CategoryStats& stat = stats[category(name)];
		stat.files++;
		stat.original += file.size;

		for (auto& part : storedParts(name, file.size)) {
			BakePackSegment segment = {static_cast<uint32_t>(part.first),
				static_cast<uint32_t>(part.second), static_cast<uint32_t>(data.size())};
			std::string bytes(part.second, '\0');
			fseek(f, part.first, SEEK_SET);
			if (fread(&bytes[0], 1, part.second, f) != static_cast<size_t>(part.second)) {
				LOG_ERROR("bake: short read from %s", file.path.c_str());
				fclose(f);
				return false;
			}
			data += bytes;
			align(data);
			segments.push_back(segment);
			stat.stored += part.second;
		}

		packFile.nSegments = segments.size() - packFile.firstSegment;
		files.push_back(packFile);
		fclose(f);

	}

	// Decoded blocks, each stored once

	std::vector<uint32_t> blobOffset(blobs.size(), UINT32_MAX);

	for (auto& item : records) {

		const std::string& name = std::get<0>(item.first);
		auto file = fileIndex.find(name);
		if (file == fileIndex.end()) continue;

		const Record& r = item.second;
		const std::string& bytes = blobs[r.blob];
		CategoryStats& stat = stats[category(name)];

		if (blobOffset[r.blob] == UINT32_MAX) {
			blobOffset[r.blob] = data.size();
			data += bytes;
			align(data);
		}

		if (!counted[r.blob]) {
			counted[r.blob] = true;
			stat.unique += bytes.size();
		}

		stat.entries++;
		stat.decoded += bytes.size();

		BakePackEntry entry = {};
		entry.file = file->second;
		entry.kind = r.kind;
		entry.param = r.param;
		entry.offset = r.offset;
		entry.end = r.end;
		entry.length = bytes.size();
		entry.dataOffset = blobOffset[r.blob];
		entries.push_back(entry);

	}

	// std::map keeps records sorted by name, offset, kind, length and param,
	// and names are sorted the same way, so the entries are already in order

	BakePackHeader header = {};
	memcpy(header.magic, BAKEPACK_MAGIC, 4);
	header.version = BAKEPACK_VERSION;
	header.nFiles = files.size();
	header.nEntries = entries.size();
	header.nSegments = segments.size();

	header.filesOffset = sizeof(header);
	header.entriesOffset = header.filesOffset + files.size() * sizeof(BakePackFile);
	header.segmentsOffset = header.entriesOffset + entries.size() * sizeof(BakePackEntry);

	// Data follows the tables
	uint32_t dataStart = header.segmentsOffset + segments.size() * sizeof(BakePackSegment);
	for (auto& s : segments) s.dataOffset += dataStart;
	for (auto& e : entries) e.dataOffset += dataStart;

	std::string tables;
	for (auto& f : files) append(tables, f);
	for (auto& e : entries) append(tables, e);
	for (auto& s : segments) append(tables, s);

	header.size = dataStart + data.size();

	FILE* out = fopen(outPath, "wb");
	if (!out) {
		LOG_ERROR("bake: cannot write %s", outPath);
		return false;
	}
	fwrite(&header, sizeof(header), 1, out);
	fwrite(tables.data(), 1, tables.size(), out);
	fwrite(data.data(), 1, data.size(), out);
	fclose(out);

	// Report

	printf("\n%-10s %5s %7s %10s %10s %10s %10s %10s\n", "category", "files", "entries",
		"original", "stored", "decoded", "unique", "in pack");

	long total[5] = {};
	for (auto& item : stats) {
		CategoryStats& s = item.second;
		long inPack = s.stored + s.unique;
		printf("%-10s %5d %7d %10ld %10ld %10ld %10ld %10ld\n", item.first.c_str(), s.files,
			s.entries, s.original, s.stored, s.decoded, s.unique, inPack);
		total[0] += s.original; total[1] += s.stored; total[2] += s.decoded;
		total[3] += s.unique; total[4] += inPack;
	}
	printf("%-10s %5zu %7zu %10ld %10ld %10ld %10ld %10ld\n", "total", files.size(),
		entries.size(), total[0], total[1], total[2], total[3], total[4]);
	printf("\n(unique counts a decoded block under the first category that uses it)\n");

	long music = 0;
	int songs = 0;
	for (auto& item : opened)
		if (category(item.first) == "*.PSM") { music += item.second.size; songs++; }
	printf("music opened during the sweep, not packed: %d files, %ld bytes\n", songs, music);

	printf("wrote %s: %u bytes\n", outPath, header.size);

	return true;

}


int bakeSweep (const char* gameDir, const char* outPath) {

	int failures = 0;

	bool ok = sweepGameData(gameDir, [&](const char* name, SweepType type, const SweepLoad& load) {
		try {
			load()();
		} catch (int e) {
			// Planets throw E_NONE when there is nothing to approach, and
			// demos of bonus levels are not supported
			if (!(type == SWEEP_PLANET && e == E_NONE) &&
				!(type == SWEEP_DEMO && e == E_DEMOTYPE)) {
				LOG_WARN("bake: %s failed to load (%d)", name, e);
				failures++;
			}
		}
	});

	if (!ok) return 1;

	printf("bake: %zu files opened, %zu decoded blocks, %zu distinct, %d items failed to load\n",
		opened.size(), records.size(), blobs.size(), failures);

	return writePack(outPath) ? 0 : 1;

}
