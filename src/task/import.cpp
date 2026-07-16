// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "import.h"

#include <atomic>
#include <fstream>
#include <format>
#include <istream>
#include <ostream>
#include <system_error>

#include "../db/table/table_import_export.h"
#include "log.h"

namespace {

static std::filesystem::path temporary_export_path(const std::filesystem::path &path) {
	static std::atomic_uint64_t nextId = 0;
	for(;;) {
		auto temporary = path;
		temporary += std::format(".tmp.{}", nextId.fetch_add(1, std::memory_order_relaxed));
		std::error_code error;
		if(!std::filesystem::exists(temporary, error) || error) return temporary;
	}
}

static void remove_file(const std::filesystem::path &path) {
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

} // namespace

bool export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	auto task_status = tc.scope(std::format("Exporting play statistics to {}", path.string()));
	log_debug("Exporting playstats: {}", path.string());

	const auto temporary = temporary_export_path(path);
	std::ofstream out(temporary, std::ios::binary);
	if(!out) {
		const auto message = std::format("Failed to open file for writing: {}", temporary.string());
		log_error("{}", message);
		tc.fail(message);
		return false;
	}

	if(!export_playstats(tc, db, out)) {
		out.close();
		remove_file(temporary);
		return false;
	}
	out.flush();
	out.close();
	if(!out) {
		const auto message = std::format("Failed to write play statistics: {}", temporary.string());
		log_error("{}", message);
		tc.fail(message);
		remove_file(temporary);
		return false;
	}

	std::error_code error;
	std::filesystem::rename(temporary, path, error);
	if(error) {
		const auto message = std::format("Failed to replace {}: {}", path.string(), error.message());
		log_error("{}", message);
		tc.fail(message);
		remove_file(temporary);
		return false;
	}
	return true;
}

bool import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	auto task_status = tc.scope(std::format("Importing play statistics from {}", path.string()));
	log_debug("Importing playstats: {}", path.string());

	std::ifstream in(path, std::ios_base::binary);
	if(!in) {
		const auto message = std::format("Failed to open file: {}", path.string());
		log_error("{}", message);
		tc.fail(message);
		return false;
	}

	return import_playstats(tc, db, in);
}

bool export_playstats(TaskControl &tc, sqlite3 *db, std::ostream &out) {
	if(db_export_table(db, "play", out)) return true;
	tc.fail("Failed to export play statistics");
	return false;
}

bool import_playstats(TaskControl &tc, sqlite3 *db, std::istream &in) {
	if(db_import_table(db, "play", in)) return true;
	tc.fail("Failed to import play statistics");
	return false;
}
