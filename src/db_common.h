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

bool db_get_file(sqlite3* db, const std::string id, FileRow &file);


bool parseMod(sqlite3* ctx, const std::string fileName, const std::span<std::byte> data);
