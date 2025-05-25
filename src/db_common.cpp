// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "db_common.h"

#include "util/hash_util.h"
#include "util/sqlite_util.h"
#include <span>
#include <sqlite3.h>
#include <vector>
#include <zstd.h>

#include "util/str_util.h"
#include "util/xmp_util.h"
#include "log.h"


bool db_get_file(sqlite3 *db, const std::string id, FileRow &file) {
	auto sql = R"(
		SELECT
			file.id,
			file.name,
			file.size,
			file.data
		FROM file
		WHERE
			(file.id == ?1)
		LIMIT 1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if(r != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}
	
	sqliteu_bind_string(stmt,  1, id);
	
	auto s = sqlite3_step(stmt);
	if(s == SQLITE_DONE) {
		return false;
	}
	if(s != SQLITE_ROW) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return false;
	}
	
	file.id = sqlite3_column_string(stmt, 0);
	file.name = sqlite3_column_string(stmt, 1);
	auto decompSize = sqlite3_column_int64(stmt, 2);
	auto compData = sqlite3_column_blob(stmt, 3);
	auto compSize = sqlite3_column_bytes(stmt, 3);
	
	file.rawData.resize(decompSize);
	auto size = ZSTD_decompress(file.rawData.data(), file.rawData.size(), compData, compSize);
	if(ZSTD_isError(size)) {
		log_error("Failed to decompress: {} -> {}", id, ZSTD_getErrorName(size));
		return false;
	}
	return true;
}



static bool fileExists(sqlite3* DB, std::string id) {
	
	auto sql = R"(
		SELECT id
		FROM meta
		WHERE id = ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(DB, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(DB));
		return false;
	}
	sqliteu_bind_string(stmt, 1, id);
	
	auto s = sqlite3_step(stmt);
	if(s == SQLITE_ROW) {
		return true;
	} else if(s == SQLITE_DONE) {
		return false;
	} else {
		log_error("step failed: {}", sqlite3_errmsg(DB));
		return false;
	}
}

struct ModMeta {
	std::string file_name;
	int64_t     file_size;
	std::string name;
	std::string type;
	int64_t     bpm;
	int64_t     duration;
};

static void db_add_file(sqlite3* db, const ModMeta &meta, const std::span<std::byte> data) {
	
	auto id = calc_sha1(data);
	bool exists = fileExists(db, id);
	
	auto md5 = calc_md5(data);
	
	//app.indexer.fileName.set(std::format("File: {} -> {}", name, exists ? "Exists": "Adding"));
	
	if(exists) {
		log_debug("File already exists: {}", meta.file_name);
		return;
	}
	
	std::vector<std::byte> buffer;
	std::span<std::byte> bufferView;
	{
		int level = ZSTD_maxCLevel();
		
		buffer.resize(ZSTD_compressBound(data.size()));
		auto size = ZSTD_compress(buffer.data(), buffer.size(), data.data(), data.size(), level);
		if(ZSTD_isError(size)) {
			log_error("Compressor error: {}", ZSTD_getErrorName(size));
			return;
		}
		bufferView = std::span<std::byte>(buffer.data(), size);
	}
	double compression = static_cast<double>(bufferView.size()) / static_cast<double>(data.size());
	
	log_debug("Adding file comp={:.2f}: {}", compression, meta.file_name);
	
	{
		auto sql = R"(
			INSERT INTO file(id, name, size, data)
			VALUES(?1, ?2, ?3, ?4)
			;
		)";
		
		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int r;
		r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
		
		sqliteu_bind_string(stmt, 1, id);
		sqliteu_bind_string(stmt, 2, meta.file_name);
		sqlite3_bind_int64 (stmt, 3, data.size());
		sqlite3_bind_blob  (stmt, 4, bufferView.data(), bufferView.size(), SQLITE_STATIC);
		
		r = sqlite3_step(stmt);
		if(r != SQLITE_DONE) {
			log_error("step failed: {}", sqlite3_errmsg(db));
			return;
		}
	}
	{
		auto sql = R"(
			INSERT INTO meta(
				id,
				md5,
				todo,
				file_name,
				file_size,
				name,
				type,
				bpm,
				duration
			)
			VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
			;
		)";
		
		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
		
		sqliteu_bind_string(stmt, 1, id);
		sqliteu_bind_string(stmt, 2, md5);
		sqlite3_bind_int64 (stmt, 3, 0);
		sqliteu_bind_string(stmt, 4, meta.file_name);
		sqlite3_bind_int64 (stmt, 5, meta.file_size);
		sqliteu_bind_string(stmt, 6, meta.name);
		sqliteu_bind_string(stmt, 7, meta.type);
		sqlite3_bind_int64 (stmt, 8, meta.bpm);
		sqlite3_bind_int64 (stmt, 9, meta.duration);
		
		r = sqlite3_step(stmt);
		if(r != SQLITE_DONE) {
			log_error("step failed: {}", sqlite3_errmsg(db));
			return;
		}
	}
}

bool parseMod(sqlite3* ctx, const std::string fileName, const std::span<std::byte> data) {
	
	int r;
	
	CLEAN_XMP xmp_context xmp = xmp_create_context();
	
	r = xmp_load_module_from_memory(xmp, data.data(), data.size());
	if(r < 0) {
		log_error("xmp error: {} -> {}", fileName.c_str(), xmpu_errstr(r));
		return false;
	}
	
	struct xmp_module_info mi;
	xmp_get_module_info(xmp, &mi);
	
	if(mi.num_sequences < 1) {
		log_error("No sequences!: {}", mi.num_sequences);
		xmp_release_module(xmp);
		return false;
	}
	
	ModMeta meta;
	meta.file_name = fileName;
	meta.file_size = data.size();
	meta.name = load_string(mi.mod->name);
	meta.type = load_string(mi.mod->type);
	meta.bpm = mi.mod->bpm;
	meta.duration = mi.seq_data[0].duration;
	trim(meta.name);
	
	xmp_release_module(xmp);
	
	db_add_file(ctx, meta, data);
	return true;
}
