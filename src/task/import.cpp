// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "import.h"

#include <fstream>

#include "log.h"
#include "util/sqlite_util.h"
#include "util/str_util.h"


static void insert_row(
	sqlite3 *db,
	std::string table,
	std::vector<std::string> &header,
	std::vector<std::string> &row
) {
	auto fields = str_join(header, ", ", [](const auto &col) {
		// Double-quote identifier; escape embedded quotes as ""
		std::string q = "\"";
		for (char c : col) {
			if (c == '"') q += "\"\"";
			else q += c;
		}
		q += "\"";
		return q;
	});

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
		table,
		fields,
		place
	);
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	//sqlite3_bind_parameter_count(stmt);
	
	for(size_t i = 0; i < row.size(); i++) {
		int r = sqliteu_bind_string(stmt, i + 1, row[i]);
		if(r != SQLITE_OK) {
			log_error("bind failed: {}", sqlite3_errmsg(db));
			return;
		}
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return;
	}
}

void export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	log_debug("Exporting playstats: {}", path.string());

	std::ofstream out(path, std::ios::binary);
	if(!out) {
		log_error("Failed to open file for writing: {}", path.string());
		return;
	}

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, "SELECT * FROM play", -1, &stmt, nullptr) != SQLITE_OK) {
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

void import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path) {
	
	log_debug("Importing: {}", path.string());

	std::ifstream infile(path, std::ios_base::binary);
	if(!infile) {
		log_error("Failed to open file: {}", path.string());
		return;
	}

	if(!sqliteu_begin(db)) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(db));
		return;
	}

	int lineIdx = -1;
	std::string line;
	std::vector<std::string> header;
	while(std::getline(infile, line)) {
		lineIdx++;
		if(lineIdx == 0) {
			header = str_split(line, "\t");
			continue;
		}

		auto rowVec = str_split(line, "\t");
		if(header.size() != rowVec.size()) {
			log_debug("Column count mismatch in file");
			continue;
		}

		insert_row(db, "play", header, rowVec);
	}

	if(!sqliteu_commit(db)) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(db));
	}
	log_debug("DONE");
}
