// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <string>

#include <sqlite3.h>

// Validate a database against the final layout of a specific epoch. Old epoch
// validators are permanent because cross-epoch imports must validate their
// source database before reading it.
bool db_validate_epoch_schema(sqlite3 *db, int epoch, std::string &error_message);
