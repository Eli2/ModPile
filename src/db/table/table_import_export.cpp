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

static void append_escaped_field(std::string &out, std::string_view field) {
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
		out.append(field.data() + chunkStart, i - chunkStart);
		out.append(escape);
		chunkStart = i + 1;
	}
	out.append(field.data() + chunkStart, field.size() - chunkStart);
}

static bool identifier_equal_ascii(std::string_view lhs, std::string_view rhs);

static bool flush_output(std::ostream &out, std::string &buffer) {
	if(buffer.empty()) return true;
	out.write(buffer.data(), buffer.size());
	buffer.clear();
	return static_cast<bool>(out);
}

enum class ExportType { Text, Integer, Real, Blob, Dynamic };

static ExportType export_type(const char *declaredType) {
	if(!declaredType) return ExportType::Dynamic;
	if(identifier_equal_ascii(declaredType, "TEXT")) return ExportType::Text;
	if(identifier_equal_ascii(declaredType, "INTEGER")) return ExportType::Integer;
	if(identifier_equal_ascii(declaredType, "REAL")) return ExportType::Real;
	if(identifier_equal_ascii(declaredType, "BLOB")) return ExportType::Blob;
	return ExportType::Dynamic;
}

static bool unescape_field(std::string_view field, std::string &unescaped) {
	unescaped.clear();
	if(unescaped.capacity() < field.size()) unescaped.reserve(field.size());
	for(size_t i = 0; i < field.size(); i++) {
		if(field[i] != '\\') {
			unescaped += field[i];
			continue;
		}
		if(i + 1 >= field.size()) return false;

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
		default: return false;
		}
	}
	return true;
}

static std::optional<std::string> unescape_field(std::string_view field) {
	std::string unescaped;
	if(!unescape_field(field, unescaped)) return std::nullopt;
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

static bool validate_encoded_field(std::string_view field) {
	if(field == "\\N") return true;
	if(field.starts_with("\\B")) {
		return base64_decode(field.substr(2)).has_value();
	}
	if(field.find('\\') == std::string_view::npos) return true;
	return unescape_field(field).has_value();
}

enum class ImportType { Text, Integer, Real };

struct ImportColumn {
	int parameter = 0; // Zero means the source column is ignored.
	ImportType type = ImportType::Text;
};

static ImportType import_type(std::string_view declaredType) {
	if(identifier_equal_ascii(declaredType, "INTEGER")) return ImportType::Integer;
	if(identifier_equal_ascii(declaredType, "REAL")) return ImportType::Real;
	return ImportType::Text;
}

static bool bind_encoded_field(
	sqlite3_stmt *stmt,
	int parameter,
	ImportType type,
	std::string_view field,
	std::string &decodeBuffer
) {
	if(field == "\\N") return sqlite3_bind_null(stmt, parameter) == SQLITE_OK;
	if(field.starts_with("\\B")) {
		auto blob = base64_decode(field.substr(2));
		if(!blob) return false;
		static constexpr std::byte emptyBlob{};
		const void *data = blob->empty() ? &emptyBlob : blob->data();
		return sqlite3_bind_blob64(stmt, parameter, data, blob->size(), SQLITE_TRANSIENT) == SQLITE_OK;
	}

	std::string_view text = field;
	if(field.find('\\') != std::string_view::npos) {
		if(!unescape_field(field, decodeBuffer)) return false;
		text = decodeBuffer;
	}

	if(type == ImportType::Integer) {
		int64_t value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if(error == std::errc{} && end == text.data() + text.size()) {
			return sqlite3_bind_int64(stmt, parameter, value) == SQLITE_OK;
		}
	} else if(type == ImportType::Real) {
		double value = 0.0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if(error == std::errc{} && end == text.data() + text.size()) {
			return sqlite3_bind_double(stmt, parameter, value) == SQLITE_OK;
		}
	}
	return sqlite3_bind_text64(stmt, parameter, text.data(), text.size(),
		SQLITE_TRANSIENT, SQLITE_UTF8) == SQLITE_OK;
}

enum class InsertLineResult { Ok, ColumnCountMismatch, InvalidField, SqliteError };

static InsertLineResult insert_line(
	sqlite3 *db,
	sqlite3_stmt *stmt,
	std::string_view line,
	const std::vector<ImportColumn> &columns,
	std::string &decodeBuffer
) {
	// Every INSERT parameter has exactly one source column and is rebound for
	// every row, so clearing all previous bindings would be redundant work.
	if(sqlite3_reset(stmt) != SQLITE_OK) {
		log_error("resetting insert statement failed: {}", sqlite3_errmsg(db));
		return InsertLineResult::SqliteError;
	}

	size_t column = 0;
	size_t pos = 0;
	while(true) {
		if(column == columns.size()) return InsertLineResult::ColumnCountMismatch;
		const auto next = line.find('\t', pos);
		const auto field = next == std::string_view::npos
			? line.substr(pos)
			: line.substr(pos, next - pos);
		const auto &mapping = columns[column++];
		const bool valid = mapping.parameter == 0
			? validate_encoded_field(field)
			: bind_encoded_field(stmt, mapping.parameter, mapping.type, field, decodeBuffer);
		if(!valid) return InsertLineResult::InvalidField;
		if(next == std::string_view::npos) break;
		pos = next + 1;
	}
	if(column != columns.size()) return InsertLineResult::ColumnCountMismatch;
	if(sqlite3_step(stmt) != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return InsertLineResult::SqliteError;
	}
	return InsertLineResult::Ok;
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
	std::vector<ExportType> exportTypes;
	exportTypes.reserve(ncols);
	for(int i = 0; i < ncols; ++i) {
		exportTypes.push_back(export_type(sqlite3_column_decltype(stmt, i)));
	}
	constexpr size_t outputBufferSize = 64 * 1024;
	std::string buffer;
	buffer.reserve(outputBufferSize);

	for(int i = 0; i < ncols; i++) {
		if(i > 0) buffer += '\t';
		append_escaped_field(buffer, sqlite3_column_name(stmt, i));
	}
	buffer += '\n';
	if(!flush_output(out, buffer)) {
		log_error("Failed to write table header");
		return false;
	}

	int rc;
	while((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		for(int i = 0; i < ncols; i++) {
			if(i > 0) buffer += '\t';
			const auto expectedType = exportTypes[i];
			if(expectedType == ExportType::Integer || expectedType == ExportType::Real) {
				const auto runtimeType = sqlite3_column_type(stmt, i);
				if(runtimeType == SQLITE_NULL) {
					buffer += "\\N";
					continue;
				}
				char number[64];
				std::to_chars_result converted;
				if(expectedType == ExportType::Integer) {
					converted = std::to_chars(
						std::begin(number), std::end(number), sqlite3_column_int64(stmt, i));
				} else {
					converted = std::to_chars(
						std::begin(number), std::end(number), sqlite3_column_double(stmt, i));
				}
				if(converted.ec != std::errc{}) {
					log_error("Failed to format numeric value in column {}", i);
					return false;
				}
				buffer.append(number, converted.ptr);
				continue;
			}
			if(expectedType == ExportType::Blob || expectedType == ExportType::Dynamic) {
				const auto runtimeType = sqlite3_column_type(stmt, i);
				if(runtimeType == SQLITE_NULL) {
					buffer += "\\N";
					continue;
				}
				if(runtimeType == SQLITE_BLOB) {
					const auto *data = sqlite3_column_blob(stmt, i);
					const auto size = static_cast<size_t>(sqlite3_column_bytes(stmt, i));
					buffer += "\\B";
					base64_encode_append(
						buffer, std::span(static_cast<const std::byte*>(data), size));
					continue;
				}
			}
			const auto *val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
			if(!val) {
				buffer += "\\N";
				continue;
			}
			const auto nbytes = static_cast<size_t>(sqlite3_column_bytes(stmt, i));
			const std::string_view value(val, nbytes);
			if(expectedType == ExportType::Text) {
				append_escaped_field(buffer, value);
			} else {
				// Dynamic columns are only possible for generated values. Their
				// runtime storage class is not represented by the declared type.
				append_escaped_field(buffer, value);
			}
		}
		buffer += '\n';
		if(buffer.size() >= outputBufferSize && !flush_output(out, buffer)) {
			log_error("Failed to write table row");
			return false;
		}
	}
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return false;
	}
	if(!flush_output(out, buffer)) {
		log_error("Failed to write final table data");
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
	std::vector<ImportColumn> importColumns;
	std::string decodeBuffer;
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

			std::vector<std::string> insertHeader;
			insertHeader.reserve(header.size());
			importColumns.reserve(header.size());
			for(const auto &name : header) {
				const auto *column = find_column(insertableColumns, name);
				if(!column) {
					importColumns.push_back({});
					continue;
				}
				if(identifier_equal_ascii(column->type, "ANY")) {
					log_error(
						"Cannot safely import column {}.{} with type ANY in a {} table: "
						"the original SQLite storage class is not encoded",
						table, column->name, *strict ? "STRICT" : "non-STRICT"
					);
					return false;
				}
				insertHeader.push_back(column->name);
				importColumns.push_back({
					static_cast<int>(insertHeader.size()), import_type(column->type)
				});
			}
			if(insertHeader.empty()) {
				log_error("Table import header contains no insertable columns");
				return false;
			}
			if(!prepare_insert(db, table, insertHeader, &insertStmt)) return false;
			continue;
		}

		switch(insert_line(db, insertStmt, line, importColumns, decodeBuffer)) {
		case InsertLineResult::Ok:
			break;
		case InsertLineResult::ColumnCountMismatch:
			log_error(
				"Column count mismatch on row {}: expected {} columns",
				lineIdx + 1, importColumns.size()
			);
			return false;
		case InsertLineResult::InvalidField:
			log_error("Invalid encoded field on row {}", lineIdx + 1);
			return false;
		case InsertLineResult::SqliteError:
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
