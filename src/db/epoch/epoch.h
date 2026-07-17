// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <optional>
#include <string>

#include <sqlite3.h>

// Validate a database against the final layout of a specific epoch. Old epoch
// validators are permanent because cross-epoch imports must validate their
// source database before reading it.
bool db_validate_epoch_schema(sqlite3 *db, int epoch, std::string &error_message);

// Final migration level belonging to an epoch. Kept with the permanent epoch
// validator so legacy imports do not depend on the current application's schema.
std::optional<int> db_epoch_final_migration_version(int epoch);
