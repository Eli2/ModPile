// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "config.h"
#include "log.h"
#include "util/toml.h"

#include <filesystem>
#include <string>
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

	if(auto v = r.get_string ("database", "path")) app.config.database.path = *v;

	if(auto v = r.get_integer("window",   "width"))     app.config.window.width  = static_cast<int>(*v);
	if(auto v = r.get_integer("window",   "height"))    app.config.window.height = static_cast<int>(*v);

	if(auto v = r.get_bool   ("numblock", "enabled"))   app.config.numblock.enabled = *v;
	if(auto v = r.get_integer("numblock", "vid"))       app.config.numblock.vid = static_cast<unsigned short>(*v);
	if(auto v = r.get_integer("numblock", "pid"))       app.config.numblock.pid = static_cast<unsigned short>(*v);

	if(auto v = r.get_float("player", "gain"))             app.player.state.gain.store(static_cast<float>(*v));
	if(auto v = r.get_float("player", "stereo_width"))    app.player.state.stereo_width.store(static_cast<float>(*v));
	if(auto v = r.get_float("player", "target_loudness")) app.config.player.target_loudness = *v;

	if(auto v = r.get_float("eq", "low"))     app.player.state.eq_low.store(static_cast<float>(*v));
	if(auto v = r.get_float("eq", "mid1"))    app.player.state.eq_mid1.store(static_cast<float>(*v));
	if(auto v = r.get_float("eq", "mid2"))    app.player.state.eq_mid2.store(static_cast<float>(*v));
	if(auto v = r.get_float("eq", "high"))    app.player.state.eq_high.store(static_cast<float>(*v));
	if(auto v = r.get_bool ("eq", "enabled")) app.player.state.eq_enabled.store(*v);
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
	w.write("width",  static_cast<int64_t>(app.config.window.width));
	w.write("height", static_cast<int64_t>(app.config.window.height));

	w.section("numblock");
	w.write("enabled", app.config.numblock.enabled);
	w.write_hex("vid", app.config.numblock.vid);
	w.write_hex("pid", app.config.numblock.pid);

	w.section("player");
	w.write("gain",             static_cast<double>(app.player.state.gain.load()));
	w.write("stereo_width",     static_cast<double>(app.player.state.stereo_width.load()));
	w.write("target_loudness",  app.config.player.target_loudness);

	w.section("eq");
	w.write("enabled", app.player.state.eq_enabled.load());
	w.write("low",     static_cast<double>(app.player.state.eq_low.load()));
	w.write("mid1",    static_cast<double>(app.player.state.eq_mid1.load()));
	w.write("mid2",    static_cast<double>(app.player.state.eq_mid2.load()));
	w.write("high",    static_cast<double>(app.player.state.eq_high.load()));
	
	if(!w.save(g_configFile)) {
		log_error("Failed to write config file: {}", g_configFile.string());
	}
}
