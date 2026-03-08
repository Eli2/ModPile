// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

// TODO mark tables as ") STRICT"

extern const char* const V001_sql = R"(

	-- BLOB colum must come last for performance
	CREATE TABLE IF NOT EXISTS file(
		id    TEXT    PRIMARY KEY  NOT NULL,
		name  TEXT                 NOT NULL,
		size  INTEGER              NOT NULL,
		data  BLOB                 NOT NULL
	)
	;

	CREATE TABLE IF NOT EXISTS meta(
		id         TEXT     PRIMARY KEY  NOT NULL,
		md5        TEXT     UNIQUE       NOT NULL,
		todo       INTEGER               NOT NULL,
		file_name  TEXT                  NOT NULL,
		file_size  INTEGER               NOT NULL,
		name       TEXT              ,
		type       TEXT              ,
		bpm        INTEGER           ,
		duration   INTEGER           ,
		loudness   REAL
	)
	;

	CREATE TABLE IF NOT EXISTS modland(
		md5    TEXT    PRIMARY KEY  NOT NULL,
		format TEXT                 NOT NULL,
		artist TEXT                 NOT NULL,
		name   TEXT                 NOT NULL,
		path   TEXT                 NOT NULL
	)
	;

	CREATE TABLE IF NOT EXISTS modland_meta(
		md5    TEXT    PRIMARY KEY  NOT NULL,
		bad    INTEGER
	)
	;

	CREATE TABLE IF NOT EXISTS modland_format(
		format        TEXT    PRIMARY KEY  NOT NULL,
		xmp_supported INTEGER              NOT NULL
	)
	;

	CREATE TABLE IF NOT EXISTS play(
		id              TEXT     PRIMARY KEY NOT NULL,
		rating_total    INTEGER              NOT NULL,
		rating_count    INTEGER              NOT NULL,
		rating          REAL     AS (rating_total / max(rating_count, 1)) STORED,
		trash           INTEGER              NOT NULL,
		played          INTEGER              NOT NULL,
		skipped         INTEGER              NOT NULL,
		duration        INTEGER              NOT NULL
	)
	;

	CREATE VIRTUAL TABLE IF NOT EXISTS text_index
	USING FTS5(
		id UNINDEXED,
		file_name,
		name,
		artist,
		prefix=2,
		tokenize='porter ascii'
	)
	;

	CREATE TABLE IF NOT EXISTS playlist (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		name TEXT NOT NULL,
		description TEXT
	)
	;

	CREATE TABLE IF NOT EXISTS playlist_track (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		playlist_id INTEGER NOT NULL,
		track_id TEXT NOT NULL,
		track_order INTEGER NOT NULL,
		FOREIGN KEY (playlist_id) REFERENCES playlist(id) ON DELETE CASCADE
	)
	;

)";
