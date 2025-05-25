// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "sqlite_util.h"

#include <optional>
#include <sqlite3.h>

void sqlite_close(sqlite3 **db) {
	sqlite3_close(*db);
}

void sqlite_free(char **db) {
	sqlite3_free(*db);
}

void sqlite_finalize(sqlite3_stmt **db) {
	sqlite3_finalize(*db);
}


std::string sqlite3_column_string(sqlite3_stmt *stmt, int iCol) {
	auto cStr = sqlite3_column_text(stmt, iCol);
	if(cStr) {
		return std::string(reinterpret_cast<const char*>(cStr));
	} else {
		return "";
	}
	
}

int sqliteu_bind_string(sqlite3_stmt *stmt, int index, std::string_view str) {
	return sqlite3_bind_text(stmt, index, str.data(), str.length(), SQLITE_TRANSIENT);
}

int sqliteu_bind_optional_string(sqlite3_stmt *stmt, int index, const std::optional<std::string> &str) {
	if(str.has_value()) {
		return sqlite3_bind_text(stmt, index, str->data(), str->length(), SQLITE_TRANSIENT);
	}
	return sqlite3_bind_null(stmt, index);
}

std::optional<int64_t> sqlite_util_column_int64(sqlite3_stmt *stmt, int colIdx) {
	auto colType = sqlite3_column_type(stmt, colIdx);
	switch (colType) {
	default:
	case SQLITE_NULL:
		return std::nullopt;
	case SQLITE_INTEGER:
		return sqlite3_column_int64(stmt, colIdx);
	}
}

std::optional<double> sqlite_util_column_double(sqlite3_stmt *stmt, int colIdx) {
	auto colType = sqlite3_column_type(stmt, colIdx);
	switch (colType) {
	default:
	case SQLITE_NULL:
		return std::nullopt;
	case SQLITE_FLOAT:
		return sqlite3_column_double(stmt, colIdx);
	}
}

bool sqliteu_begin(sqlite3 *db) {
	return sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool sqliteu_commit(sqlite3 *db) {
	if(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
		sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
		return false;
	}
	return true;
}

void sqliteu_rollback(sqlite3 *db) {
	sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
}
