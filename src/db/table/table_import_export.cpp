// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "table_import_export.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "../../log.h"
#include "../../util/coder/base64.h"
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

struct FieldValue {
	enum class Type { Text, TextView, Null, Blob, Integer, Real };
	Type type = Type::Text;
	std::string data;
	std::string_view view;
	int64_t integer = 0;
	double real = 0.0;
};

static bool prepare_insert(
	sqlite3 *db,
	const std::string &table,
	const std::vector<std::string> &header,
	sqlite3_stmt **stmt
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

	int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

static bool insert_row(sqlite3 *db, sqlite3_stmt *stmt, const std::vector<FieldValue> &row) {
	int rc = sqlite3_reset(stmt);
	if(rc != SQLITE_OK) {
		log_error("statement reset failed: {}", sqlite3_errmsg(db));
		return false;
	}
	rc = sqlite3_clear_bindings(stmt);
	if(rc != SQLITE_OK) {
		log_error("clearing statement bindings failed: {}", sqlite3_errmsg(db));
		return false;
	}
	for(size_t i = 0; i < row.size(); i++) {
		int r = SQLITE_OK;
		switch(row[i].type) {
		case FieldValue::Type::Text:
			r = sqliteu_bind_string(stmt, i + 1, row[i].data);
			break;
		case FieldValue::Type::TextView:
			r = sqlite3_bind_text64(stmt, i + 1, row[i].view.data(), row[i].view.size(),
				SQLITE_TRANSIENT, SQLITE_UTF8);
			break;
		case FieldValue::Type::Null:
			r = sqlite3_bind_null(stmt, i + 1);
			break;
		case FieldValue::Type::Blob:
			r = sqlite3_bind_blob64(
				stmt, i + 1, row[i].data.data(), row[i].data.size(), SQLITE_TRANSIENT);
			break;
		case FieldValue::Type::Integer:
			r = sqlite3_bind_int64(stmt, i + 1, row[i].integer);
			break;
		case FieldValue::Type::Real:
			r = sqlite3_bind_double(stmt, i + 1, row[i].real);
			break;
		}
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

static void write_escaped_field(std::ostream &out, std::string_view field) {
	size_t chunkStart = 0;
	for(size_t i = 0; i < field.size(); ++i) {
		std::string_view escape;
		switch(field[i]) {
		case '\\': escape = "\\\\"; break;
		case '\0': escape = "\\0";  break;
		case '\b': escape = "\\b";  break;
		case '\f': escape = "\\f";  break;
		case '\r': escape = "\\r";  break;
		case '\n': escape = "\\n";  break;
		case '\t': escape = "\\t";  break;
		case '\v': escape = "\\v";  break;
		default: continue;
		}
		out.write(field.data() + chunkStart, i - chunkStart);
		out.write(escape.data(), escape.size());
		chunkStart = i + 1;
	}
	out.write(field.data() + chunkStart, field.size() - chunkStart);
}

static std::optional<std::string> unescape_field(std::string_view field) {
	std::string unescaped;
	unescaped.reserve(field.size());
	for(size_t i = 0; i < field.size(); i++) {
		if(field[i] != '\\') {
			unescaped += field[i];
			continue;
		}
		if(i + 1 >= field.size()) return std::nullopt;

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
		default: return std::nullopt;
		}
	}
	return unescaped;
}

static void split_fields(std::string_view line, std::vector<std::string_view> &fields) {
	fields.clear();
	size_t pos = 0;
	while(true) {
		auto next = line.find('\t', pos);
		if(next == std::string_view::npos) {
			fields.push_back(line.substr(pos));
			break;
		}
		fields.push_back(line.substr(pos, next - pos));
		pos = next + 1;
	}
}

static bool identifier_equal_ascii(std::string_view lhs, std::string_view rhs);

static bool decode_fields(
	const std::vector<std::string_view> &row,
	const std::vector<std::string> &columnTypes,
	std::vector<FieldValue> &decoded
) {
	decoded.clear();
	if(decoded.capacity() < row.size()) decoded.reserve(row.size());
	for(size_t i = 0; i < row.size(); ++i) {
		const auto &field = row[i];
		// PostgreSQL COPY text convention: \N represents SQL NULL. It is
		// recognized before unescaping, so a literal "\N" (exported as "\\N")
		// remains distinct from the sentinel.
		if(field == "\\N") {
			FieldValue result;
			result.type = FieldValue::Type::Null;
			decoded.push_back(std::move(result));
		} else if(field.starts_with("\\B")) {
			auto blob = base64_decode(std::string_view(field).substr(2));
			if(!blob) return false;
			std::string blob_data;
			if(!blob->empty()) {
				blob_data.assign(reinterpret_cast<const char*>(blob->data()), blob->size());
			}
			FieldValue result;
			result.type = FieldValue::Type::Blob;
			result.data = std::move(blob_data);
			decoded.push_back(std::move(result));
		} else {
			std::optional<std::string> unescaped;
			std::string_view text = field;
			if(field.find('\\') != std::string_view::npos) {
				unescaped = unescape_field(field);
				if(!unescaped) return false;
				text = *unescaped;
			}
			if(identifier_equal_ascii(columnTypes[i], "INTEGER")) {
				int64_t value = 0;
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
				if(error == std::errc{} && end == text.data() + text.size()) {
					FieldValue result;
					result.type = FieldValue::Type::Integer;
					result.integer = value;
					decoded.push_back(std::move(result));
					continue;
				}
			} else if(identifier_equal_ascii(columnTypes[i], "REAL")) {
				double value = 0.0;
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
				if(error == std::errc{} && end == text.data() + text.size()) {
					FieldValue result;
					result.type = FieldValue::Type::Real;
					result.real = value;
					decoded.push_back(std::move(result));
					continue;
				}
			}
			FieldValue result;
			if(unescaped) {
				result.type = FieldValue::Type::Text;
				result.data = std::move(*unescaped);
			} else {
				result.type = FieldValue::Type::TextView;
				result.view = text;
			}
			decoded.push_back(std::move(result));
		}
	}
	return true;
}

static bool validate_encoded_field(std::string_view field) {
	if(field == "\\N") return true;
	if(field.starts_with("\\B")) {
		return base64_decode(field.substr(2)).has_value();
	}
	return unescape_field(field).has_value();
}

struct ColumnInfo {
	std::string name;
	std::string type;
};

static std::vector<ColumnInfo> insertable_columns(sqlite3 *db, const std::string &table) {
	auto sql = std::format("PRAGMA table_xinfo({})", quote_identifier(table));

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
	if(rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return {};
	}

	std::vector<ColumnInfo> columns;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const auto hidden = sqlite3_column_int(stmt, 6);
		if(hidden == 0) {
			columns.push_back({sqlite3_column_string(stmt, 1), sqlite3_column_string(stmt, 2)});
		}
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return {};
	}
	return columns;
}

static bool identifier_equal_ascii(std::string_view lhs, std::string_view rhs) {
	return lhs.size() == rhs.size() && std::ranges::equal(lhs, rhs, [](char a, char b) {
		return std::toupper(static_cast<unsigned char>(a)) ==
			std::toupper(static_cast<unsigned char>(b));
	});
}

static std::optional<bool> table_is_strict(sqlite3 *db, const std::string &table) {
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, "PRAGMA table_list", -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("Failed to inspect table mode: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	int rc;
	std::optional<bool> strict;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if(identifier_equal_ascii(sqlite3_column_string(stmt, 1), table)) {
			if(strict.has_value()) {
				log_error("Table name {} is ambiguous across attached schemas", table);
				return std::nullopt;
			}
			strict = sqlite3_column_int(stmt, 5) != 0;
		}
	}
	if(rc != SQLITE_DONE) {
		log_error("Failed to inspect table mode: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}
	return strict;
}

static std::vector<size_t> remove_non_insertable_columns(
	std::vector<std::string> &header,
	const std::vector<ColumnInfo> &insertableColumns
) {
	std::vector<size_t> removed;
	for(size_t i = 0; i < header.size(); i++) {
		auto column = std::ranges::find_if(insertableColumns, [&](const ColumnInfo &candidate) {
			return identifier_equal_ascii(candidate.name, header[i]);
		});
		if(column == insertableColumns.end()) {
			removed.push_back(i);
			header.erase(header.begin() + i);
			i--;
		} else {
			// Use the spelling from SQLite when constructing the INSERT statement.
			header[i] = column->name;
		}
	}
	return removed;
}

static const ColumnInfo *find_column(
	const std::vector<ColumnInfo> &columns,
	std::string_view name
) {
	auto column = std::ranges::find_if(columns, [&](const ColumnInfo &candidate) {
		return identifier_equal_ascii(candidate.name, name);
	});
	return column == columns.end() ? nullptr : &*column;
}

static bool has_duplicate_columns(const std::vector<std::string> &columns) {
	for(size_t i = 0; i < columns.size(); ++i) {
		for(size_t j = i + 1; j < columns.size(); ++j) {
			if(identifier_equal_ascii(columns[i], columns[j])) return true;
		}
	}
	return false;
}

static void remove_columns(std::vector<std::string_view> &row, const std::vector<size_t> &columns) {
	for(auto it = columns.rbegin(); it != columns.rend(); ++it) {
		if(*it < row.size()) {
			row.erase(row.begin() + *it);
		}
	}
}

static bool validate_columns(
	const std::vector<std::string_view> &row,
	const std::vector<size_t> &columns
) {
	return std::ranges::all_of(columns, [&](size_t column) {
		return column < row.size() && validate_encoded_field(row[column]);
	});
}

bool db_export_table(sqlite3 *db, const std::string &table, std::ostream &out) {
	log_debug("Exporting table {}", table);
	const auto strict = table_is_strict(db, table);
	if(!strict.has_value()) {
		log_error("Could not uniquely resolve table {}", table);
		return false;
	}
	if(!*strict) {
		log_error("Cannot safely export non-STRICT table {}", table);
		return false;
	}

	const auto columns = insertable_columns(db, table);
	if(columns.empty()) {
		log_error("No insertable columns found for table {}", table);
		return false;
	}
	for(const auto &column : columns) {
		if(column.type.empty() || identifier_equal_ascii(column.type, "ANY")) {
			log_error(
				"Cannot safely export column {}.{} without a concrete declared type",
				table, column.name
			);
			return false;
		}
	}

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	auto sql = std::format("SELECT * FROM {}", quote_identifier(table));
	if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}

	int ncols = sqlite3_column_count(stmt);

	for(int i = 0; i < ncols; i++) {
		if(i > 0) out << '\t';
		write_escaped_field(out, sqlite3_column_name(stmt, i));
	}
	out << '\n';
	if(!out) {
		log_error("Failed to write table header");
		return false;
	}

	int rc;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		for(int i = 0; i < ncols; i++) {
			if(i > 0) out << '\t';
			const auto type = sqlite3_column_type(stmt, i);
			if(type == SQLITE_NULL) {
				out << "\\N";
				continue;
			}
			if(type == SQLITE_BLOB) {
				const auto *data = sqlite3_column_blob(stmt, i);
				const auto size = static_cast<size_t>(sqlite3_column_bytes(stmt, i));
				out << "\\B" << base64_encode(
					std::span(static_cast<const std::byte*>(data), size));
				continue;
			}
			const auto *val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
			if(val) {
				const auto nbytes = static_cast<size_t>(sqlite3_column_bytes(stmt, i));
				write_escaped_field(out, std::string_view(val, nbytes));
			}
		}
		out << '\n';
		if(!out) {
			log_error("Failed to write table row");
			return false;
		}
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

bool db_import_table(sqlite3 *db, const std::string &table, std::istream &in) {
	log_debug("Importing table {}", table);

	const auto strict = table_is_strict(db, table);
	if(!strict.has_value()) {
		log_error("Could not determine whether table {} is STRICT", table);
		return false;
	}
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
	std::vector<std::string> columnTypes;
	std::vector<size_t> ignoredColumns;
	std::vector<std::string_view> rowVec;
	std::vector<FieldValue> decoded;
	size_t sourceColumnCount = 0;
	SQLITE_FINALIZE sqlite3_stmt *insertStmt = nullptr;
	bool sawHeader = false;
	while(std::getline(in, line)) {
		if(!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lineIdx++;
		if(lineIdx == 0) {
			sawHeader = true;
			std::vector<std::string_view> encodedHeader;
			split_fields(line, encodedHeader);
			header.reserve(encodedHeader.size());
			for(const auto column : encodedHeader) {
				auto decoded = unescape_field(column);
				if(!decoded) {
					log_error("Invalid escape sequence in table import header");
					return false;
				}
				header.push_back(std::move(*decoded));
			}
			if(has_duplicate_columns(header)) {
				log_error("Table import header contains duplicate column names");
				return false;
			}
			sourceColumnCount = header.size();
			ignoredColumns = remove_non_insertable_columns(header, insertableColumns);
			if(header.empty()) {
				log_error("Table import header contains no insertable columns");
				return false;
			}
			for(const auto &name : header) {
				const auto *column = find_column(insertableColumns, name);
				if(!column) return false;
				if(identifier_equal_ascii(column->type, "ANY")) {
					log_error(
						"Cannot safely import column {}.{} with type ANY in a {} table: "
						"the original SQLite storage class is not encoded",
						table, column->name, *strict ? "STRICT" : "non-STRICT"
					);
					return false;
				}
				columnTypes.push_back(column->type);
			}
			if(!prepare_insert(db, table, header, &insertStmt)) return false;
			continue;
		}

		split_fields(line, rowVec);
		if(sourceColumnCount != rowVec.size()) {
			log_error(
				"Column count mismatch on row {}: expected {}, got {}",
				lineIdx + 1, sourceColumnCount, rowVec.size()
			);
			return false;
		}
		if(!validate_columns(rowVec, ignoredColumns)) {
			log_error("Invalid encoded field in an ignored column on row {}", lineIdx + 1);
			return false;
		}
		remove_columns(rowVec, ignoredColumns);
		if(header.size() != rowVec.size()) {
			log_error(
				"Column count mismatch on row {}: expected {}, got {}",
				lineIdx + 1, header.size(), rowVec.size()
			);
			return false;
		}

		if(!decode_fields(rowVec, columnTypes, decoded)) {
			log_error("Invalid encoded field on row {}", lineIdx + 1);
			return false;
		}

		if(!insert_row(db, insertStmt, decoded)) {
			log_error("Failed to import row {}", lineIdx + 1);
			return false;
		}
	}

	if(in.bad() || (in.fail() && !in.eof())) {
		log_error("Failed while reading table import stream");
		return false;
	}
	if(!sawHeader) {
		log_error("Table import is empty and has no header");
		return false;
	}

	if(!transaction.commit()) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(db));
		return false;
	}
	return true;
}
