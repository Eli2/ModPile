// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <format>
#include "tray.h"


#include <string>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "SDL3_ttf/SDL_ttf.h"

#include "log.h"


static TTF_Font *g_font;
static SDL_Tray *g_tray;

SDL_Surface * create_icon(char car) {
	
	auto svg = std::string_view(R"(
		<svg height="32" width="32">
			<circle cx="16" cy="16" r="14" stroke="#ffffff" stroke-width="1" fill="#000000"/>
		</svg>
	)");
	
	SDL_IOStream *rw = SDL_IOFromConstMem(svg.data(), svg.size());
	SDL_Surface *surface = IMG_Load_IO(rw, true);
	
	if(g_font) {
		SDL_Color white = { 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE };
		SDL_Color black = { 0x00, 0x00, 0x00, SDL_ALPHA_TRANSPARENT };
		auto *glyph = TTF_RenderGlyph_Shaded(g_font, car, white, black);
		
		SDL_Rect r;
		r.x = (surface->w / 2) - (glyph->w / 2);
		r.y = (surface->h / 2) - (glyph->h / 2) - 1;
		SDL_BlitSurface(glyph, 0, surface, &r);
		SDL_DestroySurface(glyph);
	}
	
	return surface;
}

void tray_init(AppState &app) {
	
	// FIXME hardcoded font path
	g_font = TTF_OpenFont("../../res/Montserrat-Regular.ttf", 20);
	if(!g_font) {
		log_error("Failed to load font: {}", SDL_GetError());
	}
	
	
	auto svg = create_icon(' ');
	
	g_tray = SDL_CreateTray(svg, "ModPile");
	SDL_DestroySurface(svg);
	
	SDL_TrayMenu *menu = SDL_CreateTrayMenu(g_tray);
	
	// XXX long submenus go offscreen !
	// auto subB = SDL_InsertTrayEntryAt(menu, -1, "Foo", SDL_TRAYENTRY_SUBMENU);
	// auto subM = SDL_CreateTraySubmenu(subB);
	
	#define RATING_BTN(_name, _rating) do{ \
		auto r0 = SDL_InsertTrayEntryAt(menu, -1, _name, SDL_TRAYENTRY_BUTTON); \
		SDL_SetTrayEntryCallback(r0, [](void *userdata, SDL_TrayEntry *invoker) { \
			auto &app = *static_cast<AppState*>(userdata); \
			app.player.request.rating = _rating; \
		}, &app); \
	} while(0)
	
	RATING_BTN("Rating 0", 0);
	RATING_BTN("Rating 1", 1);
	RATING_BTN("Rating 2", 2);
	RATING_BTN("Rating 3", 3);
	RATING_BTN("Rating 4", 4);
	RATING_BTN("Rating 5", 5);
	RATING_BTN("Rating 6", 6);
	RATING_BTN("Rating 7", 7);
	RATING_BTN("Rating 8", 8);
	RATING_BTN("Rating 9", 9);
	
	// Separator
	SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON);
	
	auto quit = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
	SDL_SetTrayEntryCallback(quit, [](void *userdata, SDL_TrayEntry *invoker) {
		auto &app = *static_cast<AppState*>(userdata);
		app.request.quit = true;
	}, &app);
	
	// Separator
	SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON);
	
	auto playPause = SDL_InsertTrayEntryAt(menu, -1, "Play/Pause", SDL_TRAYENTRY_BUTTON);
	SDL_SetTrayEntryCallback(playPause, [](void *userdata, SDL_TrayEntry *invoker) {
		auto &app = *static_cast<AppState*>(userdata);
		app.player.request.playToggle = true;
	}, &app);
	
	auto next = SDL_InsertTrayEntryAt(menu, -1, "Next", SDL_TRAYENTRY_BUTTON);
	SDL_SetTrayEntryCallback(next, [](void *userdata, SDL_TrayEntry *invoker) {
		auto &app = *static_cast<AppState*>(userdata);
		app.player.request.next = true;
	}, &app);
}

static long lastRating = -1;

void tray_iterate(AppState &app) {
	if(lastRating != app.player.request.rating) {
		lastRating = app.player.request.rating;
		
		log_debug("Updating tray icon");
		
		auto str = std::to_string(lastRating);
		auto c = str.back();
		auto svg = create_icon(c);
		SDL_SetTrayIcon(g_tray, svg);
		SDL_DestroySurface(svg);
	}
}

void tray_quit() {
	SDL_DestroyTray(g_tray);
	TTF_CloseFont(g_font);
}
