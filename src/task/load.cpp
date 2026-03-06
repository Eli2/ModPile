// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "load.h"

#include <filesystem>
#include <set>
#include <span>
#include <vector>
#include <iostream>
#include <fstream>

#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>

#include "db_common.h"
#include "task_util.h"
#include "util/archive_util.h"
#include "log.h"

namespace fs = std::filesystem;

static bool parseModFoo(TaskControl &tc, sqlite3* ctx, const std::string fileName, const std::span<std::byte> data) {
	tc.statusline2.set(fileName);
	return parseMod(ctx, fileName, data);
}

static bool tooDeep(int depth) {
	if(depth > 20) {
		log_error("Nesting level too deep");
		return true;
	}
	return false;
}

static void recurseArchive(TaskControl &tc, sqlite3* db, const int depth, archive *outer) {
	
	if(tooDeep(depth) || tc.abort) {
		return;
	}
	
	archive_entry *entry;
	while(archive_read_next_header(outer, &entry) == ARCHIVE_OK) {
		auto entryName = archive_entry_pathname_utf8(entry);
		if(!entryName) {
			log_error("Null file name in archive");
			continue;
		}
		//std::cout << std::format("{}\n", entryName);
		
		bool readOk = true;
		std::vector<std::byte> allData;
		for(;;) {
			std::array<std::byte, 1024 * 8> buffer;
			la_ssize_t size = archive_read_data(outer, buffer.data(), buffer.size());
			if(size == 0) {
				break;
			}
			if(size < 0) {
				log_error("Error reading archive: {}", archive_error_string(outer));
				readOk = false;
				break;
			}
			if(allData.size() > (1024 * 1024 * 1024)) {
				log_error("Archive too large");
				readOk = false;
				break;
			}
			
			const std::span<std::byte> readData = {buffer.data(), (size_t)size};
			allData.insert(allData.end(), readData.begin(), readData.end());
		}
		
		if(readOk){
			ARCHIVE_CLEAN archive *inner = archive_read_new();
			archive_read_support_filter_all(inner);
			archive_read_support_format_all(inner);
			
			int r = archive_read_open_memory(inner, allData.data(), allData.size());
			if(r == ARCHIVE_OK) {
				recurseArchive(tc, db, depth+1, inner);
			} else {
				parseModFoo(tc, db, entryName, allData);
			}
		}
	}
}

static void readFile(TaskControl &tc, sqlite3* db, const int depth, const fs::path path) {
	
	ARCHIVE_CLEAN archive *a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	
	int r = archive_read_open_filename(a, path.c_str(), 1024 * 8);
	if(r == ARCHIVE_OK) {
		recurseArchive(tc, db, depth+1, a);
	} else {
		// Not an archive
		std::vector<std::byte> buffer;
		auto length = std::filesystem::file_size(path);
		if (length != 0) {
			buffer.resize(length);
			std::ifstream inputFile(path, std::ios_base::binary);
			inputFile.read(reinterpret_cast<char*>(buffer.data()), length);
			if(!inputFile) {
				log_error("Failed to read file: {}", path.string());
				return;
			}
		}
		
		parseModFoo(tc, db, path.filename(), buffer);
	}
}

static void recurseFilesystem(TaskControl &tc, sqlite3* db, const int depth, const fs::path path) {
	
	if(tooDeep(depth) || tc.abort) {
		return;
	}
	
	if(fs::is_directory(path)) {
		std::set<fs::path> sorted;
		
		for(auto &entry : fs::directory_iterator(path)) {
			sorted.insert(entry.path());
		}
		for(auto &entry : sorted) {
			recurseFilesystem(tc, db, depth+1, entry);
		}
	} else {
		readFile(tc, db, depth+1, path);
	}
}

void load_run(TaskControl &tc, sqlite3 *db, std::filesystem::path &path) {
	tc.statusline.set(std::format("Loading: {}", path.string()));
	recurseFilesystem(tc, db, 0, path);
}
