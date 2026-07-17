// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <optional>

#include <sqlite3.h>

// Run all pending migrations against an open database connection.
// Creates the schema_migration table on first use.
// Returns false if any migration fails (the failed migration is rolled back).
bool db_migrate(sqlite3 *db);

// Inspect without creating or migrating anything. Used for read-only imports.
std::optional<int> db_schema_version(sqlite3 *db);
