// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "table_import_export.h"

#include <algorithm>
#include <format>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "../../log.h"
#include "../../util/sqlite_util.h"
#include "../../util/str_util.h"

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
	const std::vector<std::optional<std::string>> &row
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

	for(size_t i = 0; i < row.size(); i++) {
		int r = sqliteu_bind_optional_string(stmt, i + 1, row[i]);
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

static std::string escape_field(std::string_view field) {
	std::string escaped;
	escaped.reserve(field.size());
	for(char c : field) {
		switch(c) {
		case '\\': escaped += "\\\\"; break;
		case '\0': escaped += "\\0";  break;
		case '\b': escaped += "\\b";  break;
		case '\f': escaped += "\\f";  break;
		case '\r': escaped += "\\r";  break;
		case '\n': escaped += "\\n";  break;
		case '\t': escaped += "\\t";  break;
		case '\v': escaped += "\\v";  break;
		default:
			escaped += c;
			break;
		}
	}
	return escaped;
}

static std::string unescape_field(std::string_view field) {
	std::string unescaped;
	unescaped.reserve(field.size());
	for(size_t i = 0; i < field.size(); i++) {
		if(field[i] != '\\' || i + 1 >= field.size()) {
			unescaped += field[i];
			continue;
		}

		i++;
		switch(field[i]) {
		case '\\': unescaped += '\\'; break;
		case '0':  unescaped += '\0'; break;
		case 'b':  unescaped += '\b'; break;
		case 'f':  unescaped += '\f'; break;
		case 'r':  unescaped += '\r'; break;
		case 'n':  unescaped += '\n'; break;
		case 't':  unescaped += '\t'; break;
		case 'v':  unescaped += '\v'; break;
		default:
			unescaped += '\\';
			unescaped += field[i];
			break;
		}
	}
	return unescaped;
}

static std::vector<std::string> split_fields(std::string_view line) {
	std::vector<std::string> fields;
	size_t pos = 0;
	while(true) {
		auto next = line.find('\t', pos);
		if(next == std::string_view::npos) {
			fields.emplace_back(line.substr(pos));
			break;
		}
		fields.emplace_back(line.substr(pos, next - pos));
		pos = next + 1;
	}
	return fields;
}

static std::vector<std::optional<std::string>> decode_fields(const std::vector<std::string> &row) {
	std::vector<std::optional<std::string>> decoded;
	decoded.reserve(row.size());
	for(const auto &field : row) {
		// PostgreSQL COPY text convention: \N represents SQL NULL. It is
		// recognized before unescaping, so a literal "\N" (exported as "\\N")
		// remains distinct from the sentinel.
		if(field == "\\N") {
			decoded.emplace_back(std::nullopt);
		} else {
			decoded.emplace_back(unescape_field(field));
		}
	}
	return decoded;
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

bool db_export_table(sqlite3 *db, const std::string &table, std::ostream &out) {
	log_debug("Exporting table {}", table);

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	auto sql = std::format("SELECT * FROM {}", quote_identifier(table));
	if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}

	int ncols = sqlite3_column_count(stmt);

	for(int i = 0; i < ncols; i++) {
		if(i > 0) out << '\t';
		out << sqlite3_column_name(stmt, i);
	}
	out << '\n';

	int rc;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		for(int i = 0; i < ncols; i++) {
			if(i > 0) out << '\t';
			if(sqlite3_column_type(stmt, i) == SQLITE_NULL) {
				out << "\\N";
				continue;
			}
			const auto *val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
			if(val) {
				const auto nbytes = static_cast<size_t>(sqlite3_column_bytes(stmt, i));
				out << escape_field(std::string_view(val, nbytes));
			}
		}
		out << '\n';
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

bool db_import_table(sqlite3 *db, const std::string &table, std::istream &in) {
	log_debug("Importing table {}", table);

	const auto insertableColumns = insertable_columns(db, table);
	if(insertableColumns.empty()) {
		log_error("No insertable columns found for table {}", table);
		return false;
	}

	SqliteTransaction transaction(db);
	if(!transaction.active()) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(db));
		return false;
	}

	int lineIdx = -1;
	std::string line;
	std::vector<std::string> header;
	std::vector<size_t> ignoredColumns;
	while(std::getline(in, line)) {
		if(!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lineIdx++;
		if(lineIdx == 0) {
			header = split_fields(line);
			ignoredColumns = remove_non_insertable_columns(header, insertableColumns);
			continue;
		}

		auto rowVec = split_fields(line);
		remove_columns(rowVec, ignoredColumns);
		if(header.size() != rowVec.size()) {
			log_debug("Column count mismatch in file");
			continue;
		}

		if(!insert_row(db, table, header, decode_fields(rowVec))) {
			log_error("Failed to import row {}", lineIdx + 1);
			return false;
		}
	}

	if(!transaction.commit()) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}
