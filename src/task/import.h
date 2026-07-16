// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#pragma once

#include "db_common.h"
#include "task_util.h"

#include <filesystem>
#include <iosfwd>

bool import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path);
bool export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path);
bool import_playstats(TaskControl &tc, sqlite3 *db, std::istream &in);
bool export_playstats(TaskControl &tc, sqlite3 *db, std::ostream &out);
