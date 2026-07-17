// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "database_epoch.h"

#include <format>

#include "util/sqlite_util.h"

DatabaseEpochCheck db_check_database_epoch(sqlite3 *db, int expected_epoch, int &actual_epoch) {
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK) {
		return DatabaseEpochCheck::error;
	}
	if(sqlite3_step(stmt) != SQLITE_ROW) {
		return DatabaseEpochCheck::error;
	}

	actual_epoch = sqlite3_column_int(stmt, 0);
	return actual_epoch == expected_epoch
		? DatabaseEpochCheck::compatible
		: DatabaseEpochCheck::incompatible;
}

bool db_set_database_epoch(sqlite3 *db, int epoch) {
	if(epoch < 0) {
		return false;
	}
	const auto sql = std::format("PRAGMA user_version = {}", epoch);
	return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}
