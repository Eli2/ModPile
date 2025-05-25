// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "modland.h"

#include <archive_entry.h>
#include <set>
#include <vector>
#include <ranges>
#include <string_view>

#include <curl/curl.h>
#include <zstd.h>

#include "db_common.h"
#include "task_util.h"
#include "util/archive_util.h"
#include "util/curl_util.h"
#include "util/defer_util.h"
#include "util/hash_util.h"
#include "util/sqlite_util.h"
#include "util/str_util.h"
#include "util/timer_util.h"
#include "util/xmp_util.h"
#include "log.h"


static std::set<std::string> g_modland_non_format_dirs = {
	"Ad Lib",                        // AdLib OPL music
	"Delitracker Custom",            // Amiga Delitracker custom players
	"HVSC",                          // C64 SID collection
	"IFF-SMUS",                      // Amiga IFF/SMUS structured music
	"MDX",                           // X68000 MDX format
	"MGSDRV",                        // MSX MGS format
	"Nintendo DS Sound Format",      // NDS 2SF format
	"Nintendo SPC",                  // SNES SPC format
	"Playstation 2 Sound Format",    // PS2 PSF2 format
	"Playstation Sound Format",      // PSF/miniPSF
	"S98",                           // PC-98 S98 format
	"SNDH",                          // Atari ST SNDH format
	"Spectrum",                      // ZX Spectrum platform grouping
	"Video Game Music",              // VGM/VGZ game music
};

static size_t curlwritefunction(std::byte *data, size_t size, size_t nmemb, void *clientp) {
	std::vector<std::byte> &out = *static_cast<std::vector<std::byte>*>(clientp);
	size_t realsize = size * nmemb;
	out.insert(out.end(), &data[0], &data[realsize]);
	return realsize;
}


struct ModlandRow {
	std::string md5;
	std::string format;
	std::string artist;
	std::string name;
	std::string path;
};

static void add_modland_path(sqlite3* db, const ModlandRow &row) {
	
	auto sql = R"(
		INSERT OR REPLACE INTO
		modland(md5, format, artist, name, path)
		VALUES(?1, ?2, ?3, ?4, ?5)
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	sqliteu_bind_string(stmt, 1, row.md5);
	sqliteu_bind_string(stmt, 2, row.format);
	sqliteu_bind_string(stmt, 3, row.artist);
	sqliteu_bind_string(stmt, 4, row.name);
	sqliteu_bind_string(stmt, 5, row.path);
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return;
	}
}

static void deleteOldModlandData(sqlite3* db) {
	
	auto sql = R"(
		DELETE FROM
			modland
		;
	)";
	
	SQLITE_FREE char *msg = nullptr;
	int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
	if(rc != SQLITE_OK){
		log_error("SQL error: {} -> {}", sql, msg);
	}
}

static void parseFile(sqlite3* db, std::vector<char> &allData) {
	log_debug("Parsing file");
	std::string_view sv = std::string_view(allData.begin(), allData.end());
	
	split(sv, "\n", [&](std::string_view line) {
		auto [md5, path] = split_first(line, ' ');
		if(md5.empty() || path.empty())
			return;
		
		auto pSplit = str_split(path, "/");
		if(pSplit.size() < 2) {
			return;
		}

		if(g_modland_non_format_dirs.contains(pSplit[0])) {
			return;
		}

		ModlandRow row;
		row.md5 = md5;
		row.path = path;
		
		if(pSplit.size() == 3) {
			row.format = pSplit[0];
			row.artist = pSplit[1];
			row.name = pSplit.back();
		} else if(pSplit.size() == 4) {
			row.format = pSplit[0];
			row.name = pSplit.back();
			
			if(pSplit[2].starts_with("coop-")) {
				row.artist = pSplit[1] + " | " + pSplit[2].substr(5);
			} 
		} else {
			log_debug("Unexpected path structure: {}", path);
			return;
		}
		
		add_modland_path(db, row);
	});
}

static void readArchive(sqlite3* db, const std::span<std::byte> data) {
	log_debug("Loading archive");
	
	ARCHIVE_CLEAN archive *inner = archive_read_new();
	archive_read_support_filter_all(inner);
	archive_read_support_format_all(inner);
	
	int r = archive_read_open_memory(inner, data.data(), data.size());
	if(r != ARCHIVE_OK) {
		log_error("Failed to open archive");
		return;
	}
	
	archive_entry *entry;
	while(archive_read_next_header(inner, &entry) == ARCHIVE_OK) {
		auto entryName = archive_entry_pathname_utf8(entry);
		if(!entryName) {
			log_error("Null file name in archive");
			continue;
		}
		if(std::string(entryName) != std::string("allmods_md5.txt")) {
			log_debug("Not the file we want: {}", entryName);
			archive_read_data_skip(inner);
			continue;
		}
		
		log_debug("Reading archive");
		
		bool readOk = true;
		std::vector<char> allData;
		for(;;) {
			std::array<char, 1024 * 8> buffer;
			la_ssize_t size = archive_read_data(inner, buffer.data(), buffer.size());
			if(size == 0) {
				break;
			}
			if(size < 0) {
				log_error("Error reading archive: {}", archive_error_string(inner));
				readOk = false;
				break;
			}
			if(allData.size() > (1024 * 1024 * 1024)) {
				log_error("Archive too large");
				readOk = false;
				break;
			}
			
			const std::span<char> readData = {buffer.data(), (size_t)size};
			allData.insert(allData.end(), readData.begin(), readData.end());
		}
		
		if(readOk){
			if(!sqliteu_begin(db)) {
				log_error("BEGIN failed: {}", sqlite3_errmsg(db));
				break;
			}

			deleteOldModlandData(db);
			parseFile(db, allData);

			{
				auto sql = R"(
					DELETE FROM modland_format
					WHERE format NOT IN (SELECT DISTINCT format FROM modland)
					;
				)";
				SQLITE_FREE char *msg = nullptr;
				int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
				if(rc != SQLITE_OK){
					log_error("SQL error cleaning modland_format: {}", msg);
				}
			}

			if(!sqliteu_commit(db)) {
				log_error("COMMIT failed: {}", sqlite3_errmsg(db));
			}
		}
		break;
	}
}

void modland_update_index(TaskControl &tc, sqlite3* db) {

	CURL *curl = curl_easy_init();
	SCOPE_EXIT(curl_easy_cleanup(curl););

	if(!curl) {
		log_error("Curl init failed");
		return;
	}

	auto url = "ftp://ftp.modland.com/pub/documents/allmods_md5.zip";

	std::vector<std::byte> result;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlwritefunction);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
		return *static_cast<std::atomic_bool*>(clientp) ? 1 : 0;
	});
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &tc.abort);

	log_debug("Downloading: {}", url);
	CURLcode res = curl_easy_perform(curl);
	if(res == CURLE_ABORTED_BY_CALLBACK) {
		log_debug("Download aborted");
		return;
	}
	if(res != CURLE_OK) {
		log_error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
		return;
	}

	readArchive(db, result);
	log_debug("DONE");
}



void modland_probe_formats(TaskControl &tc, sqlite3* db) {
	// Collect distinct formats not yet probed
	std::vector<std::string> formats;
	{
		auto sql = R"(
			SELECT DISTINCT format FROM modland
			WHERE format NOT IN (SELECT format FROM modland_format)
			;
		)";

		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}

		while(true) {
			auto s = sqlite3_step(stmt);
			if(s == SQLITE_DONE)
				break;
			if(s != SQLITE_ROW) {
				log_error("step failed: {}", sqlite3_errmsg(db));
				break;
			}
			formats.emplace_back(sqlite3_column_string(stmt, 0));
		}
	}

	// Pick one sample file per format, download, and probe with libxmp
	SQLITE_FINALIZE sqlite3_stmt *sample_stmt = nullptr;
	{
		auto sql = R"(
			SELECT path, md5 FROM modland WHERE format = ?1 LIMIT 1
			;
		)";
		int r = sqlite3_prepare_v2(db, sql, -1, &sample_stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
	}

	SQLITE_FINALIZE sqlite3_stmt *insert_stmt = nullptr;
	{
		auto sql = R"(
			INSERT OR REPLACE INTO modland_format(format, xmp_supported)
			VALUES(?1, ?2)
			;
		)";
		int r = sqlite3_prepare_v2(db, sql, -1, &insert_stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
	}

	for(int i = 0; auto &format : formats) {
		i++;
		if(tc.abort)
			break;

		tc.statusline.set(std::format("{}/{}", i, formats.size()));
		tc.statusline2.set(std::format("Probing: {}", format));

		// Get a sample file for this format
		sqlite3_reset(sample_stmt);
		sqliteu_bind_string(sample_stmt, 1, format);

		std::string path;
		std::string md5;
		{
			auto s = sqlite3_step(sample_stmt);
			if(s == SQLITE_DONE) {
				log_debug("No files for format: {}", format);
				continue;
			}
			if(s != SQLITE_ROW) {
				log_error("step failed: {}", sqlite3_errmsg(db));
				continue;
			}
			path = sqlite3_column_string(sample_stmt, 0);
			md5  = sqlite3_column_string(sample_stmt, 1);
		}

		// Download
		CURL *curl = curl_easy_init();
		SCOPE_EXIT(curl_easy_cleanup(curl););

		if(!curl) {
			log_error("Curl init failed");
			continue;
		}

		auto fullPath = std::string("pub/modules/") + path;
		auto url = curl_util_build_url("ftp://ftp.modland.com/", fullPath);
		if(url.empty()) {
			log_error("Failed to build URL for: {}", path);
			continue;
		}

		std::vector<std::byte> result;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlwritefunction);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

		CURLcode res = curl_easy_perform(curl);
		if(res != CURLE_OK) {
			log_error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
			continue;
		}

		// Verify MD5
		auto dataMd5 = calc_md5(result);
		if(md5 != dataMd5) {
			log_error("MD5 mismatch for format probe: {}", format);
			continue;
		}

		// Probe with libxmp
		CLEAN_XMP xmp_context xmp = xmp_create_context();
		int r = xmp_load_module_from_memory(xmp, result.data(), result.size());
		bool supported = (r >= 0);
		if(r >= 0)
			xmp_release_module(xmp);

		log_debug("Format '{}': xmp_supported={}", format, supported);

		// Store result
		sqlite3_reset(insert_stmt);
		sqliteu_bind_string(insert_stmt, 1, format);
		sqlite3_bind_int64(insert_stmt, 2, supported ? 1 : 0);

		auto s = sqlite3_step(insert_stmt);
		if(s != SQLITE_DONE) {
			log_error("step failed: {}", sqlite3_errmsg(db));
		}
	}
}

static void markBadFile(sqlite3* db, std::string md5) {
	auto sql = R"(
			INSERT INTO modland_meta(md5, bad)
			VALUES(?1, ?2)
			;
		)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	sqliteu_bind_string(stmt, 1, md5);
	sqlite3_bind_int64(stmt, 2, 1);
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return;
	}
	
}

static void downloadFile(sqlite3* db, std::string path, std::string md5) {
	auto fileName = str_split(path, "/").back();
	
	CURL * curl = curl_easy_init();
	SCOPE_EXIT(curl_easy_cleanup(curl););
	
	if(!curl) {
		log_error("Curl init failed");
		return;
	}
	
	auto fullPath = std::string("pub/modules/") + path;
	auto url = curl_util_build_url("ftp://ftp.modland.com/", fullPath);
	if(url.empty()) {
		log_error("Failed to build URL for: {}", path);
		return;
	}

	std::vector<std::byte> result;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlwritefunction);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

	log_debug("Downloading: {} from {}", fileName, url);
	CURLcode res = curl_easy_perform(curl);
	if(res != CURLE_OK) {
		log_error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
		return;
	}
	log_debug("Download Done");
	auto dataMd5 = calc_md5(result);
	if(md5 != dataMd5) {
		log_error("MD5 mismatch");
		return;
	}
	
	bool fileOk = parseMod(db, fileName, result);
	if(!fileOk) {
		markBadFile(db, md5);
	}
}

struct PathAndMd5 {
	std::string md5;
	std::string path;
};


void modland_download(TaskControl &tc, sqlite3* db) {
	std::vector<PathAndMd5> toDownload;
	{
		auto sql = R"(
				SELECT
					modland.md5,
					modland.path
				FROM
					modland
				LEFT JOIN meta
					ON (meta.md5 == modland.md5)
				LEFT JOIN modland_meta
					ON (modland.md5 == modland_meta.md5)
				JOIN modland_format
					ON (modland.format == modland_format.format)
				WHERE
					(meta.id IS NULL) AND
					(modland_meta.bad IS NULL) AND
					(modland_format.xmp_supported == 1)
				;
			)";
		
		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if(r != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
		
		while(true) {
			auto s = sqlite3_step(stmt);
			if(s == SQLITE_DONE) {
				break;
			}
			if(s != SQLITE_ROW) {
				log_error("step failed: {}", sqlite3_errmsg(db));
				break;
			}
			
			PathAndMd5 toDl;
			toDl.md5 = sqlite3_column_string(stmt, 0);
			toDl.path = sqlite3_column_string(stmt, 1);
			toDownload.emplace_back(toDl);
		}
	}
	
	for(int i = 0; auto &toDl: toDownload) {
		i++;
		if(tc.abort) {
			break;
		}
		
		tc.statusline.set(std::format("{}/{}", i, toDownload.size()));
		tc.statusline2.set(std::format("Downloading: {}", toDl.path));
		downloadFile(db, toDl.path, toDl.md5);
	}
}
