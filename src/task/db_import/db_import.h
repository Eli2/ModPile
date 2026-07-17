// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "../../task_util.h"

struct DatabaseImportOptions {
	bool files = true;
	bool playstats = true;
	bool playlists = true;
};

struct DatabaseImportInspection {
	bool compatible = false;
	int migration_version = 0;
	int epoch = 0;
	std::string error_message;
};

// Opens the source read-only and validates stamp, migration, epoch, then schema.
DatabaseImportInspection inspect_database_import(const std::filesystem::path &path);

// Add the selected source data without deleting or replacing destination data.
// Large categories commit in batches, so completed batches survive an abort.
bool import_database(
	TaskControl &tc,
	sqlite3 *db,
	const std::filesystem::path &path,
	const DatabaseImportOptions &options);
