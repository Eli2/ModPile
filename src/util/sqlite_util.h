// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <optional>
#include <string>

#include <sqlite3.h>


void sqlite_close(sqlite3 **db);
#define SQLITE_CLOSE __attribute__((__cleanup__(sqlite_close)))

void sqlite_free(char **db);
#define SQLITE_FREE __attribute__((__cleanup__(sqlite_free)))

void sqlite_finalize(sqlite3_stmt **db);
#define SQLITE_FINALIZE __attribute__((__cleanup__(sqlite_finalize)))

std::string sqlite3_column_string(sqlite3_stmt *stmt, int iCol);


int sqliteu_bind_string(sqlite3_stmt *stmt, int index, std::string_view str);
int sqliteu_bind_optional_string(sqlite3_stmt *stmt, int index, const std::optional<std::string> &str);


std::optional<int64_t> sqlite_util_column_int64(sqlite3_stmt *stmt, int colIdx);
std::optional<double>  sqlite_util_column_double(sqlite3_stmt *stmt, int colIdx);


bool sqliteu_begin(sqlite3 *db);
bool sqliteu_commit(sqlite3 *db);
void sqliteu_rollback(sqlite3 *db);

class SqliteTransaction {
	sqlite3 *db = nullptr;
	bool transaction_active = false;

public:
	explicit SqliteTransaction(sqlite3 *db);
	~SqliteTransaction();

	SqliteTransaction(const SqliteTransaction&) = delete;
	SqliteTransaction& operator=(const SqliteTransaction&) = delete;

	bool active() const;
	bool commit();
	void rollback();
};


inline constexpr int sqlite_base_err(int e) {
	return (e & 255);
}

static_assert(SQLITE_BUSY == sqlite_base_err(SQLITE_BUSY_SNAPSHOT));

