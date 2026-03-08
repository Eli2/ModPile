// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include "../task_util.h"
#include <filesystem>
#include <sqlite3.h>

void vacuum_run(TaskControl &tc, sqlite3 *db, std::filesystem::path path);
