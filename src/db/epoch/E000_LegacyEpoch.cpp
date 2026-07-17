// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "epoch_internal.h"

#include <array>

namespace {

constexpr std::array schema_migration_columns {
	EpochExpectedColumn {"version", "INTEGER"},
	EpochExpectedColumn {"description", "TEXT"},
	EpochExpectedColumn {"installed_on", "INTEGER"},
	EpochExpectedColumn {"execution_ms", "INTEGER"},
	EpochExpectedColumn {"success", "INTEGER"},
};
constexpr std::array file_columns {
	EpochExpectedColumn {"id", "TEXT"},
	EpochExpectedColumn {"name", "TEXT"},
	EpochExpectedColumn {"size", "INTEGER"},
	EpochExpectedColumn {"data", "BLOB"},
};
constexpr std::array meta_columns {
	EpochExpectedColumn {"id", "TEXT"},
	EpochExpectedColumn {"md5", "TEXT"},
	EpochExpectedColumn {"todo", "INTEGER"},
	EpochExpectedColumn {"file_name", "TEXT"},
	EpochExpectedColumn {"file_size", "INTEGER"},
	EpochExpectedColumn {"name", "TEXT"},
	EpochExpectedColumn {"type", "TEXT"},
	EpochExpectedColumn {"bpm", "INTEGER"},
	EpochExpectedColumn {"duration", "INTEGER"},
	EpochExpectedColumn {"loudness", "REAL"},
	EpochExpectedColumn {"audible_duration", "INTEGER"},
};
constexpr std::array modland_columns {
	EpochExpectedColumn {"md5", "TEXT"},
	EpochExpectedColumn {"format", "TEXT"},
	EpochExpectedColumn {"artist", "TEXT"},
	EpochExpectedColumn {"name", "TEXT"},
	EpochExpectedColumn {"path", "TEXT"},
};
constexpr std::array modland_meta_columns {
	EpochExpectedColumn {"md5", "TEXT"},
	EpochExpectedColumn {"bad", "INTEGER"},
};
constexpr std::array modland_format_columns {
	EpochExpectedColumn {"format", "TEXT"},
	EpochExpectedColumn {"xmp_supported", "INTEGER"},
};
constexpr std::array play_columns {
	EpochExpectedColumn {"id", "TEXT"},
	EpochExpectedColumn {"rating_total", "INTEGER"},
	EpochExpectedColumn {"rating_count", "INTEGER"},
	EpochExpectedColumn {"rating", "REAL"},
	EpochExpectedColumn {"trash", "INTEGER"},
	EpochExpectedColumn {"played", "INTEGER"},
	EpochExpectedColumn {"skipped", "INTEGER"},
	EpochExpectedColumn {"duration", "INTEGER"},
};
constexpr std::array text_index_columns {
	EpochExpectedColumn {"id", ""},
	EpochExpectedColumn {"file_name", ""},
	EpochExpectedColumn {"name", ""},
	EpochExpectedColumn {"artist", ""},
};
constexpr std::array playlist_columns {
	EpochExpectedColumn {"id", "INTEGER"},
	EpochExpectedColumn {"name", "TEXT"},
	EpochExpectedColumn {"description", "TEXT"},
};
constexpr std::array playlist_track_columns {
	EpochExpectedColumn {"id", "INTEGER"},
	EpochExpectedColumn {"playlist_id", "INTEGER"},
	EpochExpectedColumn {"track_id", "TEXT"},
	EpochExpectedColumn {"track_order", "INTEGER"},
};

// Final schema for epoch 0. This file is permanent: future cross-epoch import
// operations use it to validate legacy source databases before reading them.
constexpr std::array expected_tables {
	EpochExpectedTable {"schema_migration", schema_migration_columns},
	EpochExpectedTable {"file", file_columns},
	EpochExpectedTable {"meta", meta_columns},
	EpochExpectedTable {"modland", modland_columns},
	EpochExpectedTable {"modland_meta", modland_meta_columns},
	EpochExpectedTable {"modland_format", modland_format_columns},
	EpochExpectedTable {"play", play_columns},
	EpochExpectedTable {"text_index", text_index_columns, false},
	EpochExpectedTable {"playlist", playlist_columns},
	EpochExpectedTable {"playlist_track", playlist_track_columns},
};

} // namespace

bool db_validate_epoch_000_schema(sqlite3 *db, std::string &error_message) {
	return db_validate_epoch_tables(db, expected_tables, error_message);
}
