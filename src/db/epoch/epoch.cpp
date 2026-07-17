// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "epoch.h"

#include <string>

#include "epoch_internal.h"
#include "../../log.h"
#include "../../util/sqlite_util.h"

namespace {

bool validate_table(sqlite3 *db, const EpochExpectedTable &table, std::string &error_message) {
	const char *sql = table.include_hidden_columns
		? "SELECT name, type FROM pragma_table_xinfo(?1) ORDER BY cid"
		: "SELECT name, type FROM pragma_table_info(?1) ORDER BY cid";

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK
		|| sqlite3_bind_text(stmt, 1, table.name.data(), static_cast<int>(table.name.size()), SQLITE_STATIC) != SQLITE_OK) {
		error_message = "Could not inspect database table: " + std::string(table.name);
		return false;
	}

	std::size_t column_index = 0;
	while(true) {
		const int rc = sqlite3_step(stmt);
		if(rc == SQLITE_DONE) {
			break;
		}
		if(rc != SQLITE_ROW) {
			error_message = "Could not inspect database table: " + std::string(table.name);
			return false;
		}
		if(column_index >= table.columns.size()) {
			error_message = "Database table has unexpected columns: " + std::string(table.name);
			return false;
		}

		const auto *actual_name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		const auto *actual_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		const auto &expected = table.columns[column_index];
		if(!actual_name || !actual_type
			|| actual_name != expected.name
			|| actual_type != expected.type) {
			error_message = "Database table has an unexpected column at "
				+ std::string(table.name) + '[' + std::to_string(column_index) + ']';
			return false;
		}
		++column_index;
	}

	if(column_index != table.columns.size()) {
		error_message = "Database table is missing columns: " + std::string(table.name);
		return false;
	}
	return true;
}

} // namespace

bool db_validate_epoch_tables(
	sqlite3 *db,
	std::span<const EpochExpectedTable> expected_tables,
	std::string &error_message
) {
	for(const auto &table : expected_tables) {
		if(!validate_table(db, table, error_message)) {
			log_error("Database epoch schema validation failed: {}", error_message);
			return false;
		}
	}
	error_message.clear();
	return true;
}

bool db_validate_epoch_schema(sqlite3 *db, int epoch, std::string &error_message) {
	switch(epoch) {
	case 0:
		return db_validate_epoch_000_schema(db, error_message);
	default:
		error_message = "Unsupported database epoch: " + std::to_string(epoch);
		log_error("{}", error_message);
		return false;
	}
}

std::optional<int> db_epoch_final_migration_version(int epoch) {
	switch(epoch) {
	case 0:
		return 3;
	default:
		return std::nullopt;
	}
}
