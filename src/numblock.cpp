// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "numblock.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#include "SDL3/SDL_thread.h"

#include "util/str_util.h"
#include "util/thread_util.h"
#include "log.h"


static std::atomic_bool g_numblockThreadRunning = true;
static std::thread g_numblockThread;
static SDL_hid_device *g_hidDevice = nullptr;
static std::condition_variable g_numblockThreadCv;
static std::mutex g_numblockThreadMutex;

static std::string sdl_wchar_to_utf8(const wchar_t *str) {
	char *utf8 = SDL_iconv_wchar_utf8(str);
	if(!utf8) {
		return "";
	}
	std::string result(utf8);
	SDL_free(utf8);
	return result;
}

void numblock_init(AppState &app) {

	int r;

	// SDL defaults to enumerating only game controllers; we need a keyboard.
	SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "0");

	r = SDL_hid_init();
	if(r) {
		log_error("hid init failed: {}", SDL_GetError());
		return;
	}

	g_numblockThreadRunning = true;

	g_numblockThread = thread_create("Numblock", [&](){
		int r;
		long openBackoff = 1000;
		while(g_numblockThreadRunning) {

			if(!app.config.numblock.enabled) {
				std::unique_lock<std::mutex> lock(g_numblockThreadMutex);
				g_numblockThreadCv.wait_for(lock, std::chrono::milliseconds(500),
					[] { return !g_numblockThreadRunning.load(); });
				continue;
			}

			if(g_hidDevice) {
				log_debug("closing old hid device");
				SDL_hid_close(g_hidDevice);
				g_hidDevice = nullptr;
			}
			
			g_hidDevice = SDL_hid_open(
				app.config.numblock.vid,
				app.config.numblock.pid,
				nullptr
			);
			if(!g_hidDevice) {
				log_error("hid open failed: {}", SDL_GetError());
				openBackoff = std::min(openBackoff * 2, 2 * 60 * 1000l);
				{
					std::unique_lock<std::mutex> lock(g_numblockThreadMutex);
					g_numblockThreadCv.wait_for(
						lock,
						std::chrono::milliseconds(openBackoff),
						[] { return !g_numblockThreadRunning.load(); }
					);
				}
				if(!g_numblockThreadRunning) {
					break;
				}
				continue;
			}
			openBackoff = 1000;
			
			std::array<wchar_t, 256> manStrBuf;
			std::array<wchar_t, 256> prodStrBuf;
			int manOk = SDL_hid_get_manufacturer_string(g_hidDevice, manStrBuf.data(), manStrBuf.size());
			int prodOk = SDL_hid_get_product_string(g_hidDevice, prodStrBuf.data(), prodStrBuf.size());
			if(!manOk && !prodOk) {
				log_debug(
					"Opened hid device: {} : {}",
					sdl_wchar_to_utf8(manStrBuf.data()),
					sdl_wchar_to_utf8(prodStrBuf.data())
				);
			}
			
			
			std::array<bool, 256> lastState; 
			lastState.fill(false);
			
			while(g_numblockThreadRunning) {
				std::array<unsigned char, 65> dataBuf;
				constexpr int kReadTimeoutMs = 100;
				
				// Request state (cmd 0x81). The first byte is the report number (0x0).
				dataBuf[0] = 0x0;
				dataBuf[1] = 0x81;
				r = SDL_hid_write(g_hidDevice, dataBuf.data(), dataBuf.size());
				if(r == -1) {
					log_error("write failed: {}", SDL_GetError());
					break;
				}
				
				// Read requested state
				r = SDL_hid_read_timeout(g_hidDevice, dataBuf.data(), dataBuf.size(), kReadTimeoutMs);
				if(r == -1) {
					log_error("read failed: {}", SDL_GetError());
					break;
				}
				if(r == 0) {
					continue;
				}
				
				// auto mod = dataBuf[0];
				auto pressed = std::array<unsigned char, 6> {
					dataBuf[2],
					dataBuf[3],
					dataBuf[4],
					dataBuf[5],
					dataBuf[6],
					dataBuf[7]
				};
				
				std::array<bool, 256> keyState;
				keyState.fill(false);
				for(size_t u = 0; u < pressed.size(); u++) {
					auto scancode = pressed[u];
					// 0x00 indicates there is no scancode and no key being pressed
					// 0x01 indicates the phantom condition we just explained
					// 0x02 indicates the keyboard's self-test failed
					// 0x03 indicates an undefined error occured
					if(scancode > 3) {
						keyState[scancode] = true;
					}
				}
				
				for(size_t scancode = 0; scancode < keyState.size(); scancode++) {
					auto ks = keyState[scancode];
					if(ks != lastState[scancode]) {
						if(ks) {
							auto rate = [&](long rating) {
								app.player.request.rating = rating;
								app.player.track.rating = rating;
							};
							switch(scancode) {
							case 42: // Backspace
								app.player.request.trash = true;
								app.player.request.next = true;
								break;
							case 83: // enter
								app.player.request.playToggle = true;
								break;
							case 88: // enter
								app.player.request.next = true;
								break;
							case 89: // 1
								rate(1);
								break;
							case 90: // 2
								rate(2);
								break;
							case 91: // 3
								rate(3);
								break;
							case 92: // 4
								rate(4);
								break;
							case 93: // 5
								rate(5);
								break;
							case 94: // 6
								rate(6);
								break;
							case 95: // 7
								rate(7);
								break;
							case 96: // 8
								rate(8);
								break;
							case 97: // 9
								rate(9);
								break;
							case 98: // 0
								rate(0);
								break;
							default:
								break;
							}
							
							log_debug("Key down scancode: {}", scancode);
						}
					}
				}
				lastState = keyState;
			}
		}

		if(g_hidDevice) {
			SDL_hid_close(g_hidDevice);
			g_hidDevice = nullptr;
		}
		
		SDL_CleanupTLS();
	});
}

void numblock_quit() {
	g_numblockThreadRunning = false;
	g_numblockThreadCv.notify_all();
	if(g_numblockThread.joinable())
		g_numblockThread.join();
	SDL_hid_exit();
}

void numblock_restart(AppState &app) {
	numblock_quit();
	numblock_init(app);
}
