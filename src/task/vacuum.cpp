// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "vacuum.h"

#include <format>
#include <sqlite3.h>

#include "../log.h"

namespace {

struct VacuumProgress {
	TaskControl &tc;
	int64_t      count = 0;
};

static int vacuum_progress_handler(void *userdata) {
	auto *p = static_cast<VacuumProgress *>(userdata);
	p->count++;
	p->tc.statusline.set(std::format("{} ops", p->count));
	return p->tc.abort ? 1 : 0;
}

} // namespace

void vacuum_run(TaskControl &tc, sqlite3 *db, std::filesystem::path path) {
	// Escape single quotes in the path for the SQL string literal
	std::string escaped = path.string();
	for(size_t pos = 0; (pos = escaped.find('\'', pos)) != std::string::npos; pos += 2)
		escaped.replace(pos, 1, "''");

	VacuumProgress progress{tc};
	sqlite3_progress_handler(db, 1000, vacuum_progress_handler, &progress);

	auto sql = std::format("VACUUM INTO '{}'", escaped);
	char *errmsg = nullptr;
	int   rc     = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);

	sqlite3_progress_handler(db, 0, nullptr, nullptr);

	if(rc == SQLITE_INTERRUPT) {
		tc.statusline.set("Aborted");
		return;
	}
	if(rc != SQLITE_OK) {
		log_error("VACUUM INTO failed: {}", errmsg ? errmsg : "unknown");
		tc.statusline.set("Failed");
		sqlite3_free(errmsg);
		return;
	}

	tc.statusline.set(std::format("Done ({} ops)", progress.count));
}
