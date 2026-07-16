// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "import.h"

#include <fstream>
#include <istream>
#include <ostream>

#include "../db/table/table_import_export.h"
#include "log.h"

void export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	auto task_status = tc.scope(std::format("Exporting play statistics to {}", path.string()));
	log_debug("Exporting playstats: {}", path.string());

	std::ofstream out(path, std::ios::binary);
	if(!out) {
		log_error("Failed to open file for writing: {}", path.string());
		return;
	}

	export_playstats(tc, db, out);
}

void import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	auto task_status = tc.scope(std::format("Importing play statistics from {}", path.string()));
	log_debug("Importing playstats: {}", path.string());

	std::ifstream in(path, std::ios_base::binary);
	if(!in) {
		log_error("Failed to open file: {}", path.string());
		return;
	}

	import_playstats(tc, db, in);
}

void export_playstats(TaskControl &tc, sqlite3 *db, std::ostream &out) {
	db_export_table(db, "play", out);
}

void import_playstats(TaskControl &tc, sqlite3 *db, std::istream &in) {
	db_import_table(db, "play", in);
}
