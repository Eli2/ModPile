// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "util/ring_buffer.h"

using Guard = std::lock_guard<std::mutex>;

class LockedString {
	std::mutex mutex;
	std::string inner;
public:
	std::string get() {
		std::string ret;
		{
			auto g = std::lock_guard<std::mutex>(mutex);
			ret = inner;
		}
		return ret;
	}
	void set(const std::string_view & other) {
		auto g = std::lock_guard<std::mutex>(mutex);
		inner = other;
	}
	
};




namespace fs = std::filesystem;

struct SDL_Window;
struct SDL_GLContextState;

struct AppState {
	SDL_Window *window = nullptr;
	SDL_GLContextState *gl_context;
	
	struct Config {
		struct Window {
			int width = 800;
			int height = 600;
		} window;
		struct Database {
			fs::path path;  // empty = not configured
		} database;
		struct Player {
			struct EqualizerSettings {
				static constexpr float min_db = -18.0f;
				static constexpr float max_db = 18.0f;

				bool enabled = true;
				float low_db  = 0.0f;
				float mid1_db = 0.0f;
				float mid2_db = 0.0f;
				float high_db = 0.0f;

				bool operator==(const EqualizerSettings &) const = default;
			};
			static_assert(std::is_trivially_copyable_v<EqualizerSettings>);

			double target_loudness = -14;
			std::atomic<float> gain = 1.0f;
			std::atomic<float> stereo_width = 0.4f;
			std::atomic<EqualizerSettings> equalizer = EqualizerSettings{};
			std::atomic<bool> skip_trailing_silence = true;
		} player;
		struct NumBlock {
			bool           enabled = false;
			unsigned short vid = 0x1a2c;
			unsigned short pid = 0x2124;
		} numblock;
	} config;
	struct Setup {
		bool active = false;
		std::mutex pending_mutex;
		std::optional<std::string> pending_path;  // set from dialog callback thread
		std::string error_message;
	} setup;
	struct Request {
		std::atomic_bool quit = false;
		std::atomic_bool switch_database = false;
	} request;
	struct Player {
		struct Request {
			std::atomic_bool prev = false;
			std::atomic_bool play = false;
			std::atomic_bool pause = false;
			std::atomic_bool playToggle = false;
			std::atomic_bool stop = false;
			std::atomic_bool next = false;
			
			std::atomic_long rating = -1;
			std::atomic_bool trash = false;
			
			std::atomic<int64_t> seek     = std::numeric_limits<int64_t>::min();
			std::atomic<int64_t> position = std::numeric_limits<int64_t>::min();
			
			LockedString playId;
			std::atomic<int64_t> playlistTrackId = 0;
			std::atomic<int64_t> playlistId = 0;
			std::atomic<bool>    charts_mode = false;
		} request;
		struct State {
			enum class Playback {
				Stop,
				Play,
				Pause
			};
			std::atomic<Playback> playback_status = Playback::Stop;
			enum class Loop {
				None,
				Track,
				Playlist
			};
			std::atomic<Loop>    loop_status = Loop::None;
			std::atomic<bool>    shuffle = false;
			std::atomic<int64_t> current_playlist_track_id = 0;
			std::atomic<int64_t> current_playing_playlist_id = 0;
			std::atomic<bool>    in_charts_mode = false;
		} state;
		struct Track {
			LockedString     id;
			LockedString     file_name;
			LockedString     name;
			std::atomic<int>   length;
			std::atomic<int>   elapsed;
			std::atomic<float> gain = 1.0f;
			std::atomic_long   rating = -1;
		} track;
	} player;
	struct Pile {
		struct Request {
			bool executeQuery = false;
		} request;
		struct State {
			struct Query {
				enum class Order {
					asc,
					dsc
				};
				struct SortSpec {
					std::string col;
					Order order = Order::asc;
				};
				std::string query;
				std::vector<SortSpec> sortSpecs = {{"meta.file_name", Order::asc}};
				long offset = 0;
				long limit = 20;
			} query;
			struct Response {
				struct Row {
					std::string id;
					std::string file_name;
					int64_t     file_size;
					std::string name;
					std::optional<int64_t>     bpm;
					std::optional<int64_t>     duration;
					std::optional<double>      loudness;
					std::optional<double>      rating;
					std::string	artist;
				};
				std::vector<Row> rows;
			} response;
		} state;
	} pile;
	struct Playlist {
		struct Request {
			struct CreatePlaylist {
				std::string name;
				std::optional<std::string> description;
			};
			struct UpdatePlaylist {
				int64_t id = 0;
				std::string name;
				std::optional<std::string> description;
				bool clearDescription = false;
			};
			struct DeletePlaylist {
				int64_t id = 0;
			};
			struct AddTrack {
				int64_t playlist_id = 0;
				std::string track_id;
			};
			struct RemoveTrack {
				int64_t playlist_track_id = 0;
				int64_t playlist_id = 0;
				int64_t track_order = 0;
			};
			struct MoveTrack {
				int64_t playlist_track_id = 0;
				int64_t playlist_id = 0;
				int64_t from_order = 0;
				int64_t to_order = 0;
			};
			struct ClearPlaylist {
				int64_t playlist_id = 0;
			};
			std::optional<CreatePlaylist> createPlaylist;
			std::optional<UpdatePlaylist> updatePlaylist;
			std::optional<DeletePlaylist> deletePlaylist;
			std::optional<AddTrack> addTrack;
			std::optional<RemoveTrack> removeTrack;
			std::optional<MoveTrack> moveTrack;
			std::optional<ClearPlaylist> clearPlaylist;
			std::optional<int64_t> loadPlaylist;
			bool reloadPlaylists = false;
		} request;
		struct State {
			struct PlaylistRow {
				int64_t id = 0;
				std::string name;
				std::optional<std::string> description;
				int64_t track_count = 0;
			};
			std::vector<PlaylistRow> playlists;
			std::optional<int64_t> current_playlist_id;
			struct Row {
				int64_t playlist_track_id = 0;
				int64_t playlist_id = 0;
				int64_t track_order = 0;
				std::string id;
				std::string file_name;
				int64_t     file_size;
				std::string name;
				std::optional<int64_t>     bpm;
				std::optional<int64_t>     duration;
				std::optional<double>      loudness;
				std::optional<double>      rating;
				std::string	artist;
			};
			std::vector<Row> rows;
		} state;
	} playlist;
	struct Mpris {
		struct Request {
			std::atomic_bool metadata_changed = false;
			std::atomic_bool seek_changed = false;
			std::atomic_bool gain_changed = false;
		} request;
	} mpris;
	struct Charts {
		enum class Criterion {
			TopRated,
			MostPlayed,
			MostDuration,
		};
		struct Request {
			std::optional<Criterion> load;
		} request;
		struct State {
			std::optional<Criterion> active;
			struct Row {
				std::string id;
				std::string file_name;
				int64_t     file_size = 0;
				std::string name;
				std::string artist;
				std::optional<int64_t> bpm;
				std::optional<int64_t> duration;
				std::optional<double>  loudness;
				std::optional<double>  rating;
				std::optional<int64_t> played;
				std::optional<int64_t> play_duration;
			};
			std::vector<Row> rows;
		} state;
		struct Nav {
			std::mutex mutex;
			bool active = false;
			std::vector<std::string> track_ids;
		} nav;
	} charts;
	struct Visualizer {
		// Written by player thread, read by main thread (SPSC safe)
		SPSCRingBuffer<float, 65536> sample_queue;
	} visualizer;
};

// Reinitialize transient application state while retaining the window, graphics
// context, and user configuration. Call only after subsystem threads and
// connections have been stopped.
void reset_transient_app_state(AppState &app);
