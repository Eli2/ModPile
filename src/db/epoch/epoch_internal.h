// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <span>
#include <string>
#include <string_view>

#include <sqlite3.h>

struct EpochExpectedColumn {
	std::string_view name;
	std::string_view type;
};

struct EpochExpectedTable {
	std::string_view name;
	std::span<const EpochExpectedColumn> columns;
	bool include_hidden_columns = true;
};

bool db_validate_epoch_tables(
	sqlite3 *db,
	std::span<const EpochExpectedTable> expected_tables,
	std::string &error_message
);

bool db_validate_epoch_000_schema(sqlite3 *db, std::string &error_message);
