// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "config.h"
#include "log.h"
#include "util/toml.h"

#include <algorithm>
#include <filesystem>
#include <SDL3/SDL_stdinc.h>

namespace fs = std::filesystem;

static fs::path g_configFile;
static fs::path g_layoutFile;

// ─── Layout path ──────────────────────────────────────────────────────────────

fs::path config_get_layout_path() { return g_layoutFile; }

// ─── Load ─────────────────────────────────────────────────────────────────────

void config_load(AppState &app) {
	fs::path configDir;

	auto env = SDL_GetEnvironment();
	auto configHome = SDL_GetEnvironmentVariable(env, "XDG_CONFIG_HOME");
	if(configHome) {
		configDir = fs::path(configHome).append("ModPile");
	} else {
		auto home = SDL_GetEnvironmentVariable(env, "HOME");
		if(home) {
			configDir = fs::path(home).append(".config").append("ModPile");
		} else {
			configDir = "./";
		}
	}

	log_info("Using config path: {}", configDir.string());

	g_configFile = configDir / "ModPile.cfg";
	g_layoutFile = configDir / "imgui_layout.ini";

	if(!fs::exists(g_configFile)) return;

	TomlReader r;
	if(!r.load(g_configFile)) {
		log_error("Failed to read config file: {}", g_configFile.string());
		return;
	}

	r.get(app.config.database.path, "database", "path");

	r.get(app.config.window.width,  "window", "width");
	r.get(app.config.window.height, "window", "height");

	r.get(app.config.numblock.enabled, "numblock", "enabled");
	r.get(app.config.numblock.vid,     "numblock", "vid");
	r.get(app.config.numblock.pid,     "numblock", "pid");

	r.get(app.config.player.gain,                  "player", "gain");
	r.get(app.config.player.stereo_width,          "player", "stereo_width");
	r.get(app.config.player.target_loudness,       "player", "target_loudness");
	r.get(app.config.player.skip_trailing_silence, "player", "skip_trailing_silence");

	auto equalizer = app.config.player.equalizer.load();
	auto clamp_db = [](float value) {
		return std::clamp(
			value,
			AppState::Config::Player::EqualizerSettings::min_db,
			AppState::Config::Player::EqualizerSettings::max_db);
	};
	
	r.get(equalizer.low_db,  "eq", "low");
	r.get(equalizer.mid1_db, "eq", "mid1");
	r.get(equalizer.mid2_db, "eq", "mid2");
	r.get(equalizer.high_db, "eq", "high");
	r.get(equalizer.enabled, "eq", "enabled");

	equalizer.low_db  = clamp_db(equalizer.low_db);
	equalizer.mid1_db = clamp_db(equalizer.mid1_db);
	equalizer.mid2_db = clamp_db(equalizer.mid2_db);
	equalizer.high_db = clamp_db(equalizer.high_db);
	
	app.config.player.equalizer.store(equalizer);
}

// ─── Save ─────────────────────────────────────────────────────────────────────

void config_save(AppState &app) {
	auto parent = g_configFile.parent_path();
	if(!fs::exists(parent)) {
		log_debug("Creating parent path: {}", parent.string());
		fs::create_directories(parent);
	}

	TomlWriter w;

	w.section("database");
	w.write("path", app.config.database.path.string());

	w.section("window");
	w.write("width",  app.config.window.width);
	w.write("height", app.config.window.height);

	w.section("numblock");
	w.write("enabled", app.config.numblock.enabled);
	w.write_hex("vid", app.config.numblock.vid);
	w.write_hex("pid", app.config.numblock.pid);

	w.section("player");
	w.write("gain",             app.config.player.gain.load());
	w.write("stereo_width",     app.config.player.stereo_width.load());
	w.write("target_loudness",  app.config.player.target_loudness);
	w.write("skip_trailing_silence", app.config.player.skip_trailing_silence.load());

	const auto equalizer = app.config.player.equalizer.load();
	w.section("eq");
	w.write("enabled", equalizer.enabled);
	w.write("low",     equalizer.low_db);
	w.write("mid1",    equalizer.mid1_db);
	w.write("mid2",    equalizer.mid2_db);
	w.write("high",    equalizer.high_db);
	
	if(!w.save(g_configFile)) {
		log_error("Failed to write config file: {}", g_configFile.string());
	}
}
