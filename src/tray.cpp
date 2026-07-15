// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <format>
#include "tray.h"


#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "log.h"
#include "util/xml_util.h"


static SDL_Tray *g_tray;

static constexpr std::string_view kSegmentOn  = "#ffffff";
static constexpr std::string_view kSegmentOff = "transparent";

static SDL_Surface *create_icon(char digit) {
	auto svg = std::string(R"(
		<svg xmlns='http://www.w3.org/2000/svg' height='32' width='32' viewBox='0 0 32 32'>
			<rect x='4' y='4' width='24' height='24' rx='6' stroke='#ffffff' stroke-width='1' fill='#000000'/>
			<rect id='seg-A' x='11' y='6.5'  width='10' height='3' rx='1.2' fill='#ffffff'/>
			<rect id='seg-B' x='20' y='9'    width='3'  height='6' rx='1.2' fill='#ffffff'/>
			<rect id='seg-C' x='20' y='17'   width='3'  height='6' rx='1.2' fill='#ffffff'/>
			<rect id='seg-D' x='11' y='22.5' width='10' height='3' rx='1.2' fill='#ffffff'/>
			<rect id='seg-E' x='9'  y='17'   width='3'  height='6' rx='1.2' fill='#ffffff'/>
			<rect id='seg-F' x='9'  y='9'    width='3'  height='6' rx='1.2' fill='#ffffff'/>
			<rect id='seg-G' x='11' y='14.5' width='10' height='3' rx='1.2' fill='#ffffff'/>
		</svg>
	)");

	auto set_segment = [&](char segment, std::string_view color) {
		auto id = std::format("seg-{}", segment);
		if(!set_xml_attribute_by_id(svg, id, "fill", color)) {
			log_error("Failed to update tray icon segment {}", segment);
		}
	};

	// Seven-segment layout:
	//   AAA
	//  F   B
	//  F   B
	//   GGG
	//  E   C
	//  E   C
	//   DDD
	auto A = [&] { set_segment('A', kSegmentOn); };
	auto B = [&] { set_segment('B', kSegmentOn); };
	auto C = [&] { set_segment('C', kSegmentOn); };
	auto D = [&] { set_segment('D', kSegmentOn); };
	auto E = [&] { set_segment('E', kSegmentOn); };
	auto F = [&] { set_segment('F', kSegmentOn); };
	auto G = [&] { set_segment('G', kSegmentOn); };

	set_segment('A', kSegmentOff);
	set_segment('B', kSegmentOff);
	set_segment('C', kSegmentOff);
	set_segment('D', kSegmentOff);
	set_segment('E', kSegmentOff);
	set_segment('F', kSegmentOff);
	set_segment('G', kSegmentOff);

	switch(digit) {
	case '0': A(); B(); C(); D(); E(); F();      break;
	case '1':      B(); C();                     break;
	case '2': A(); B();      D(); E();      G(); break;
	case '3': A(); B(); C(); D();           G(); break;
	case '4':      B(); C();           F(); G(); break;
	case '5': A();      C(); D();      F(); G(); break;
	case '6': A();      C(); D(); E(); F(); G(); break;
	case '7': A(); B(); C();                     break;
	case '8': A(); B(); C(); D(); E(); F(); G(); break;
	case '9': A(); B(); C(); D();      F(); G(); break;
	case ' ':
	default:
		break;
	}
	
	SDL_IOStream *rw = SDL_IOFromConstMem(svg.data(), svg.size());
	return IMG_Load_IO(rw, true);
}

void tray_init(AppState &app) {
	auto svg = create_icon(' ');
	
	g_tray = SDL_CreateTray(svg, "ModPile");
	if(svg) {
		SDL_DestroySurface(svg);
	}
	if(!g_tray) {
		log_error("Failed to create tray: {}", SDL_GetError());
		return;
	}
	
	SDL_TrayMenu *menu = SDL_CreateTrayMenu(g_tray);
	if(!menu) {
		log_error("Failed to create tray menu: {}", SDL_GetError());
		return;
	}
	
	// XXX long submenus go offscreen !
	// auto subB = SDL_InsertTrayEntryAt(menu, -1, "Foo", SDL_TRAYENTRY_SUBMENU);
	// auto subM = SDL_CreateTraySubmenu(subB);
	
	#define RATING_BTN(_name, _rating) do{ \
		auto r0 = SDL_InsertTrayEntryAt(menu, -1, _name, SDL_TRAYENTRY_BUTTON); \
		SDL_SetTrayEntryCallback(r0, [](void *userdata, SDL_TrayEntry *invoker) { \
			auto &app = *static_cast<AppState*>(userdata); \
			app.player.request.rating = _rating; \
			app.player.track.rating = _rating; \
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
	if(!g_tray) {
		return;
	}
	const auto rating = app.player.track.rating.load();
	if(lastRating != rating) {
		lastRating = rating;
		
		log_debug("Updating tray icon");
		
		char c = lastRating >= 0 && lastRating <= 9
			? static_cast<char>('0' + lastRating)
			: ' ';
		auto svg = create_icon(c);
		if(svg) {
			SDL_SetTrayIcon(g_tray, svg);
			SDL_DestroySurface(svg);
		} else {
			log_error("Failed to create tray icon: {}", SDL_GetError());
		}
	}
}

void tray_quit() {
	if(g_tray) {
		SDL_DestroyTray(g_tray);
		g_tray = nullptr;
	}
}
