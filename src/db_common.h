// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <span>
#include <sqlite3.h>
#include <string>
#include <vector>

struct FileRow {
	std::string id;
	std::string name;
	std::vector<std::byte> rawData;
};

struct ParsedModuleMetadata {
	std::string sha1;
	std::string md5;
	std::string file_name;
	int64_t file_size = 0;
	std::string name;
	std::string type;
	int64_t bpm = 0;
	int64_t duration = 0;
};

bool db_get_file(sqlite3* db, const std::string id, FileRow &file);

bool parseModMetadata(
	const std::string &fileName,
	std::span<const std::byte> data,
	ParsedModuleMetadata &metadata
);
bool parseMod(sqlite3* ctx, const std::string fileName, const std::span<std::byte> data);
