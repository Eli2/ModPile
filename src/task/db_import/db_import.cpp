// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "db_import.h"

#include <format>

#include "db_import_internal.h"
#include "../../db.h"
#include "../../db/database_epoch.h"
#include "../../db/epoch/epoch.h"
#include "../../db/schema/schema.h"
#include "../../log.h"
#include "../../util/sqlite_util.h"

namespace {

sqlite3 *open_source(const std::filesystem::path &path) {
	sqlite3 *source = nullptr;
	const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_EXRESCODE
		| SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW;
	const int rc = sqlite3_open_v2(path.c_str(), &source, flags, nullptr);
	if(rc != SQLITE_OK) {
		log_error("Could not open database import source {}: {}", path.string(),
			source ? sqlite3_errmsg(source) : sqlite3_errstr(rc));
		if(source) sqlite3_close(source);
		return nullptr;
	}
	sqlite3_extended_result_codes(source, 1);
	return source;
}

bool read_pragma_int(sqlite3 *db, const char *sql, int &value) {
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_ROW) return false;
	value = sqlite3_column_int(stmt, 0);
	return true;
}

bool import_path_supported(int source_epoch, int destination_epoch) {
	return source_epoch == 0 && destination_epoch == 0;
}

DatabaseImportInspection inspect_open_source(sqlite3 *source, int destination_epoch) {
	DatabaseImportInspection result;
	int application_id = 0;
	if(!read_pragma_int(source, "PRAGMA application_id", application_id)) {
		result.error_message = "Could not read the selected database stamp.";
		return result;
	}
	if(application_id != MODPILE_APPLICATION_ID) {
		result.error_message = "The selected file is not stamped as a ModPile database.";
		return result;
	}

	const auto migration = db_schema_version(source);
	if(!migration.has_value()) {
		result.error_message = "Could not read the selected database migration version.";
		return result;
	}
	result.migration_version = *migration;

	if(!read_pragma_int(source, "PRAGMA user_version", result.epoch)) {
		result.error_message = "Could not read the selected database epoch.";
		return result;
	}
	const auto final_migration = db_epoch_final_migration_version(result.epoch);
	if(!final_migration) {
		result.error_message = std::format("The selected database uses unsupported epoch {}.", result.epoch);
		return result;
	}
	if(*migration != *final_migration) {
		result.error_message = std::format(
			"The selected epoch {} database migration is V{}; V{} is required.",
			result.epoch, *migration, *final_migration);
		return result;
	}
	if(!db_validate_epoch_schema(source, result.epoch, result.error_message)) return result;
	if(!import_path_supported(result.epoch, destination_epoch)) {
		result.error_message = std::format("Importing database epoch {} into epoch {} is not supported.",
			result.epoch, destination_epoch);
		return result;
	}
	result.compatible = true;
	return result;
}

bool dispatch_import(TaskControl &tc, sqlite3 *source, int source_epoch,
		sqlite3 *destination, int destination_epoch, const DatabaseImportOptions &options) {
	if(source_epoch == 0 && destination_epoch == 0) {
		return import_database_epoch_000_to_000(tc, source, destination, options);
	}
	log_error("No database importer for epoch {} -> {}", source_epoch, destination_epoch);
	return false;
}

} // namespace

DatabaseImportInspection inspect_database_import(const std::filesystem::path &path) {
	SQLITE_CLOSE sqlite3 *source = open_source(path);
	if(!source) {
		DatabaseImportInspection result;
		result.error_message = "Could not open the selected database for reading.";
		return result;
	}
	return inspect_open_source(source, MODPILE_DATABASE_EPOCH);
}

bool import_database(TaskControl &tc, sqlite3 *db, const std::filesystem::path &path,
		const DatabaseImportOptions &options) {
	auto status = tc.scope(std::format("Importing database {}", path.filename().string()));
	if(!options.files && !options.playstats && !options.playlists) {
		tc.fail("Nothing was selected for import.");
		return false;
	}

	const char *destination_name = sqlite3_db_filename(db, "main");
	std::error_code ec;
	if(destination_name && std::filesystem::equivalent(path, destination_name, ec) && !ec) {
		tc.fail("The active database cannot be imported into itself.");
		return false;
	}

	SQLITE_CLOSE sqlite3 *source = open_source(path);
	if(!source) {
		tc.fail("Could not open the selected database for import.");
		return false;
	}
	const auto inspection = inspect_open_source(source, MODPILE_DATABASE_EPOCH);
	if(!inspection.compatible) {
		tc.fail(inspection.error_message);
		return false;
	}

	const bool ok = dispatch_import(tc, source, inspection.epoch, db,
		MODPILE_DATABASE_EPOCH, options);
	if(!ok) {
		if(tc.abort) tc.aborted("Database import aborted; completed batches were kept.");
		else tc.fail("Database import failed; completed batches were kept.");
		return false;
	}
	tc.succeed("Database import completed.");
	return true;
}
