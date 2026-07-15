// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#pragma once

#include "db_common.h"
#include "task_util.h"

#include <iosfwd>

void import_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path);
void export_playstats(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path);
void import_playstats(TaskControl &tc, sqlite3 *db, std::istream &in);
void export_playstats(TaskControl &tc, sqlite3 *db, std::ostream &out);
