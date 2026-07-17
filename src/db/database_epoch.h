// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <array>
#include <optional>

#include <sqlite3.h>

#include "version_config.h"

struct DatabaseEpochMapping {
	int app_major;
	int app_minor;
	int database_epoch;
};

// Patch releases within one major/minor family always use the same database
// epoch. Changing an epoch makes databases incompatible: it is not a schema
// migration and data may cross that boundary only through an explicit import.
inline constexpr std::array DATABASE_EPOCH_MAPPINGS {
	DatabaseEpochMapping {0, 0, 0},
	DatabaseEpochMapping {0, 1, 0},
};

constexpr std::optional<int> database_epoch_for_app_version(int major, int minor) {
	for(const auto &mapping : DATABASE_EPOCH_MAPPINGS) {
		if(mapping.app_major == major && mapping.app_minor == minor) {
			return mapping.database_epoch;
		}
	}
	return std::nullopt;
}

inline constexpr auto CONFIGURED_DATABASE_EPOCH =
	database_epoch_for_app_version(MODPILE_VERSION_MAJOR, MODPILE_VERSION_MINOR);
static_assert(CONFIGURED_DATABASE_EPOCH.has_value(),
	"The configured ModPile version has no database epoch mapping");
inline constexpr int MODPILE_DATABASE_EPOCH = *CONFIGURED_DATABASE_EPOCH;

enum class DatabaseEpochCheck {
	compatible,
	incompatible,
	error,
};

// Existing databases are compatible only when their epoch is exactly equal to
// the application's epoch. Epochs are identifiers, not ordered migration levels.
DatabaseEpochCheck db_check_database_epoch(sqlite3 *db, int expected_epoch, int &actual_epoch);

// Use only after a fresh database's schema was created successfully.
bool db_set_database_epoch(sqlite3 *db, int epoch);
