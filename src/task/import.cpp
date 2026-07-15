// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "import.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "log.h"
#include "util/sqlite_util.h"
#include "util/str_util.h"


static std::string quote_identifier(const std::string &identifier) {
	std::string quoted = "\"";
	for(char c : identifier) {
		if(c == '"') {
			quoted += "\"\"";
		} else {
			quoted += c;
		}
	}
	quoted += "\"";
	return quoted;
}

static bool insert_row(
	sqlite3 *db,
	const std::string &table,
	const std::vector<std::string> &header,
	const std::vector<std::string> &row
) {
	auto fields = str_join(header, ", ", quote_identifier);

	size_t idx = 0;
	auto place = str_join(header, ", ", [&](const auto &) {
		return std::format("?{}", ++idx);
	});
	
	auto sql = std::format(
		R"(
			INSERT INTO {}({})
			VALUES({})
			;
		)",
		quote_identifier(table),
		fields,
		place
	);
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}
	
	//sqlite3_bind_parameter_count(stmt);
	
	for(size_t i = 0; i < row.size(); i++) {
		int r = sqliteu_bind_string(stmt, i + 1, row[i]);
		if(r != SQLITE_OK) {
			log_error("bind failed: {}", sqlite3_errmsg(db));
			return false;
		}
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

static std::vector<std::string> insertable_columns(sqlite3 *db, const std::string &table) {
	auto sql = std::format("PRAGMA table_xinfo({})", quote_identifier(table));

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
	if(rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return {};
	}

	std::vector<std::string> columns;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const auto hidden = sqlite3_column_int(stmt, 6);
		if(hidden == 0) {
			columns.push_back(sqlite3_column_string(stmt, 1));
		}
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return {};
	}
	return columns;
}

static std::vector<size_t> remove_non_insertable_columns(
	std::vector<std::string> &header,
	const std::vector<std::string> &insertableColumns
) {
	std::vector<size_t> removed;
	for(size_t i = 0; i < header.size(); i++) {
		if(std::ranges::find(insertableColumns, header[i]) == insertableColumns.end()) {
			removed.push_back(i);
			header.erase(header.begin() + i);
			i--;
		}
	}
	return removed;
}

static void remove_columns(std::vector<std::string> &row, const std::vector<size_t> &columns) {
	for(auto it = columns.rbegin(); it != columns.rend(); ++it) {
		if(*it < row.size()) {
			row.erase(row.begin() + *it);
		}
	}
}

static void export_table(
	TaskControl &tc,
	sqlite3 *db,
	const std::string &table,
	std::ostream &out
) {
	log_debug("Exporting table {}", table);

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	auto sql = std::format("SELECT * FROM {}", quote_identifier(table));
	if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}

	int ncols = sqlite3_column_count(stmt);

	// Write header from column names
	for(int i = 0; i < ncols; i++) {
		if(i > 0) out << '\t';
		out << sqlite3_column_name(stmt, i);
	}
	out << '\n';

	int rc;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		for(int i = 0; i < ncols; i++) {
			if(i > 0) out << '\t';
			const char *val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
			if(val) out << val;
		}
		out << '\n';
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
	}
	log_debug("DONE");
}

static void import_table(
	TaskControl &tc,
	sqlite3 *db,
	const std::string &table,
	std::istream &in
) {
	
	log_debug("Importing table {}", table);

	const auto insertableColumns = insertable_columns(db, table);
	if(insertableColumns.empty()) {
		log_error("No insertable columns found for table {}", table);
		return;
	}

	SqliteTransaction transaction(db);
	if(!transaction.active()) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(db));
		return;
	}

	int lineIdx = -1;
	std::string line;
	std::vector<std::string> header;
	std::vector<size_t> ignoredColumns;
	while(std::getline(in, line)) {
		lineIdx++;
		if(lineIdx == 0) {
			header = str_split(line, "\t");
			ignoredColumns = remove_non_insertable_columns(header, insertableColumns);
			continue;
		}

		auto rowVec = str_split(line, "\t");
		remove_columns(rowVec, ignoredColumns);
		if(header.size() != rowVec.size()) {
			log_debug("Column count mismatch in file");
			continue;
		}

		if(!insert_row(db, table, header, rowVec)) {
			log_error("Failed to import row {}", lineIdx + 1);
			return;
		}
	}

	if(!transaction.commit()) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(db));
	}
	log_debug("DONE");
}

void export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	log_debug("Exporting playstats: {}", path.string());

	std::ofstream out(path, std::ios::binary);
	if(!out) {
		log_error("Failed to open file for writing: {}", path.string());
		return;
	}

	export_playstats(tc, db, out);
}

void import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	log_debug("Importing playstats: {}", path.string());

	std::ifstream in(path, std::ios_base::binary);
	if(!in) {
		log_error("Failed to open file: {}", path.string());
		return;
	}

	import_playstats(tc, db, in);
}

void export_playstats(TaskControl &tc, sqlite3 *db, std::ostream &out) {
	export_table(tc, db, "play", out);
}

void import_playstats(TaskControl &tc, sqlite3 *db, std::istream &in) {
	import_table(tc, db, "play", in);
}
