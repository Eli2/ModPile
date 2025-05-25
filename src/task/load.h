// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#pragma once

#include "db_common.h"
#include "task_util.h"

void load_run(TaskControl &tc, sqlite3 *db, std::filesystem::path &path);
