// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

extern const char* const V002_sql = R"(

	CREATE TABLE play_new(
		id              TEXT     PRIMARY KEY NOT NULL,
		rating_total    INTEGER              NOT NULL,
		rating_count    INTEGER              NOT NULL,
		rating          REAL     AS (rating_total * 1.0 / max(rating_count, 1)) STORED,
		trash           INTEGER              NOT NULL,
		played          INTEGER              NOT NULL,
		skipped         INTEGER              NOT NULL,
		duration        INTEGER              NOT NULL
	) STRICT
	;

	INSERT INTO play_new(
		id,
		rating_total,
		rating_count,
		trash,
		played,
		skipped,
		duration
	)
	SELECT
		id,
		rating_total,
		rating_count,
		trash,
		played,
		skipped,
		duration
	FROM play
	;

	DROP TABLE play
	;

	ALTER TABLE play_new RENAME TO play
	;

)";
