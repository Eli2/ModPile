// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <iosfwd>
#include <string>

#include <sqlite3.h>

bool db_export_table(sqlite3 *db, const std::string &table, std::ostream &out);
bool db_import_table(sqlite3 *db, const std::string &table, std::istream &in);
