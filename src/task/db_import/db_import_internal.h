// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <sqlite3.h>

#include "db_import.h"

bool import_database_epoch_000_to_000(
	TaskControl &tc,
	sqlite3 *source,
	sqlite3 *destination,
	const DatabaseImportOptions &options);
