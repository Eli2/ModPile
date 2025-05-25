// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#pragma once

#include "db_common.h"
#include "task_util.h"

void modland_update_index(TaskControl &tc, sqlite3* db);
void modland_probe_formats(TaskControl &tc, sqlite3* db);
void modland_download(TaskControl &tc, sqlite3* db);
