// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "util/curl_util.h"
#include "xmp.h"
#include <archive.h>
#include <curl/curl.h>
#include <curl/curlver.h>
#include <ebur128.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <utility>

#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include "glad/glad.h"
#include "backends/imgui_impl_sdl3.h"

#include "charts.h"
#include "config.h"
#include "query.h"
#include "db.h"
#include "global.h"
#include "version_config.h"
#include "gui/gui.h"
#include "log.h"
#include "mpris.h"
#include "numblock.h"
#include "player.h"
#include "playlist.h"
#include "setup.h"
#include "task.h"
#include "tray.h"
#include "visualizer.h"

#include <AL/al.h>
#include <zstd.h>

// Shut down all subsystems that were started by app_full_init.
static void app_full_quit(AppState &app) {
	task_quit(app);
	db_query_quit();
	numblock_quit();
	tray_quit();
	mpris_quit();
	visualizer_quit(app);
	charts_quit();
	playlist_quit();
	player_quit();
}

// Initialize all subsystems that require a configured database path.
// Called either from AppInit (path already known) or from AppIterate (after setup).
static bool app_full_init(AppState &app) {
	auto database_result = db_init(app.config.database.path);
	if(!database_result.success) {
		app.setup.error_message = std::move(database_result.error_message);
		return false;
	}
	player_init(app);
	playlist_init(app);
	charts_init(app);
	visualizer_init(app);
	mpris_init(app);
	tray_init(app);
	numblock_init(app);
	db_query_init(app);
	task_init(app);

	auto title = std::string("ModPile - ") + app.config.database.path.filename().string();
	SDL_SetWindowTitle(app.window, title.c_str());
	return true;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	*appstate = new AppState;
	AppState& app = *static_cast<AppState*>(*appstate);

	// Disable buffering of stdout
	// setvbuf(stdout, nullptr, _IONBF, 0);

	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

	SDL_SetAppMetadata("ModPile", MODPILE_VERSION, "de.eli2.ModPile");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		log_error("Couldn't initialize SDL: {}", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	config_load(app);

	{
		auto sdlVersion = SDL_GetVersion();
		auto sdlMajor = SDL_VERSIONNUM_MAJOR(sdlVersion);
		auto sdlMinor = SDL_VERSIONNUM_MINOR(sdlVersion);
		auto sdlMicro = SDL_VERSIONNUM_MICRO(sdlVersion);
		log_info("SDL version: {}.{}.{}", sdlMajor, sdlMinor, sdlMicro);
	}
	{
		auto sqliteVersion = sqlite3_libversion();
		log_info("SQLite version: {}", sqliteVersion);
	}
	{
		auto xmpVersion = xmp_version;
		log_info("libxmp version: {}", xmpVersion);
	}
	{
		auto version = archive_version_number();
		auto major = version / 1000000;
		auto minor = (((version) / 1000) % 1000);
		auto patch = ((version) % 1000);
		log_info("libarchive version: {}.{}.{}", major, minor, patch);
	}
	{
		int major, minor, patch;
		ebur128_get_version(&major, &minor, &patch);
		log_info("libebur128 version: {}.{}.{}", major, minor, patch);
	}
	{
		auto c = curl_version_info(CURLVERSION_NOW);
		log_info("curl version: {}", c->version);
	}
	{
		auto version = ZSTD_versionString();
		log_info("ZSTD version: {}", version);
	}
	{
		auto version = IMGUI_VERSION;
		log_info("IMGUI: {}", version);
	}


	// GL 3.2 Core + GLSL 150
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	app.window = SDL_CreateWindow(
		"ModPile",
		app.config.window.width,
		app.config.window.height,
		SDL_WINDOW_OPENGL |
		SDL_WINDOW_RESIZABLE |
		SDL_WINDOW_HIDDEN
	);
	if(!app.window) {
		log_error("Couldn't create window/renderer: {}", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	app.gl_context = SDL_GL_CreateContext(app.window);
	if(app.gl_context == nullptr) {
		printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_GL_MakeCurrent(app.window, app.gl_context);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		std::cout << "Failed to initialize GLAD" << std::endl;
		return SDL_APP_FAILURE;
	}

	SDL_ShowWindow(app.window);

	const std::string svg =
		"<svg height='200' width='200'><circle cx='100' cy='100' r='80' stroke='white' stroke-width='4' fill='black'/></svg>";

	SDL_IOStream *rw = SDL_IOFromConstMem(svg.c_str(), svg.size());
	SDL_Surface *surface = IMG_Load_IO(rw, true);
	if(surface) {
		SDL_SetWindowIcon(app.window, surface);
		SDL_DestroySurface(surface);
	}

	curl_util_init();
	gui_init(app);

	if(app.config.database.path.empty()) {
		app.setup.active = true;
	} else {
		if(!app_full_init(app)) {
			return SDL_APP_FAILURE;
		}
	}

	//app.player.request.play = true;

	return SDL_APP_CONTINUE;
}

static void handleKeyboard(AppState& app, SDL_KeyboardEvent &e) {

	switch(e.key) {
	case SDLK_LEFT:
		app.player.request.seek.store(-1000 * 10);
		break;
	case SDLK_RIGHT:
		app.player.request.seek.store(1000 * 10);
		break;
	}
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	AppState& app = *static_cast<AppState*>(appstate);

	ImGui_ImplSDL3_ProcessEvent(event);

	switch(event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_DROP_FILE: {
			auto f = event->drop.data;
			log_debug("Dropped file {}", f);
			task_load(f);
			break;
		}
		case SDL_EVENT_KEY_DOWN:
			handleKeyboard(app, event->key);
			break;
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
	AppState& app = *static_cast<AppState*>(appstate);

	static Uint64 last_frame_ns = SDL_GetTicksNS();
	constexpr Uint64 target_frame_ns = 1'000'000'000ULL / 60;
	Uint64 now = SDL_GetTicksNS();
	Uint64 elapsed = now - last_frame_ns;
	if (elapsed < target_frame_ns) {
		SDL_DelayNS(target_frame_ns - elapsed);
	}
	last_frame_ns = SDL_GetTicksNS();

	if(app.request.switch_database.exchange(false)) {
		app_full_quit(app);
		app.setup.active = true;
		app.setup.error_message.clear();
	}

	if(app.setup.active) {
		setup_iterate(app);
		if(!app.setup.active) {
			// Setup just completed — initialize everything now
			if(!app_full_init(app)) {
				app.setup.active = true;
				if(app.setup.error_message.empty())
					app.setup.error_message = "Failed to open database at the chosen path. Please try again.";
			}
		}
		return SDL_APP_CONTINUE;
	}

	gui_iterate(app);
	player_iterate(app);
	playlist_iterate(app);
	charts_iterate(app);
	mpris_iterate(app);
	tray_iterate(app);
	db_query_iterate(app);

	if(app.request.quit) {
		return SDL_APP_SUCCESS;
	}

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	AppState& app = *static_cast<AppState*>(appstate);

	if(!app.setup.active) {
		app_full_quit(app);
	}
	gui_quit(app);

	curl_util_quit();

	SDL_GetWindowSize(
		app.window,
		&app.config.window.width,
		&app.config.window.height
	);
	config_save(app);

	SDL_DestroyWindow(app.window);
	delete static_cast<AppState*>(appstate);
}
