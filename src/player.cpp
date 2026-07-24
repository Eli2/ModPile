// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <format>
#include "player.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <optional>
#include <random>
#include <thread>
#include <sqlite3.h>
#include <xmp.h>

#include "charts.h"
#include "visualizer.h"
#include "db_common.h"
#include "global.h"
#include "log.h"
#include "db.h"
#include "mpris.h"
#include "util/defer_util.h"
#include "util/sqlite_util.h"
#include "util/str_util.h"
#include "util/thread_util.h"
#include "util/xmp_util.h"

// #include <AL/al.h>
// #include <AL/alc.h>
#include <al.h>
#include <alc.h>
#include <alext.h>
#include <efx.h>


using namespace std::chrono_literals;
using std::this_thread::sleep_for;

static std::atomic_bool g_quitRequest = false;
static std::thread g_playThread;

#define AL_CHECK \
do { \
	auto err = alGetError(); \
	if(err != AL_NO_ERROR) { \
		log_error("Al error: {}", alGetString(err)); \
	} \
} while(0)

namespace {

class OpenAlEqualizer {
	using Settings = AppState::Config::Player::EqualizerSettings;

	std::atomic<Settings> &m_settings;
	Settings m_previousSettings;

	ALuint m_source = 0;
	ALuint m_auxSlot = 0;
	ALuint m_effect = 0;
	ALuint m_silentFilter = 0;
	bool m_available = false;
	bool m_functionsLoaded = false;

	LPALGENEFFECTS                 m_alGenEffects = nullptr;
	LPALDELETEEFFECTS              m_alDeleteEffects = nullptr;
	LPALEFFECTI                    m_alEffecti = nullptr;
	LPALEFFECTF                    m_alEffectf = nullptr;
	LPALGENAUXILIARYEFFECTSLOTS    m_alGenAuxSlots = nullptr;
	LPALDELETEAUXILIARYEFFECTSLOTS m_alDelAuxSlots = nullptr;
	LPALAUXILIARYEFFECTSLOTI       m_alAuxSloti = nullptr;
	LPALGENFILTERS                 m_alGenFilters = nullptr;
	LPALDELETEFILTERS              m_alDeleteFilters = nullptr;
	LPALFILTERI                    m_alFilteri = nullptr;
	LPALFILTERF                    m_alFilterf = nullptr;

	template<typename Function>
	bool load_function(Function &function, const char *name) {
		function = reinterpret_cast<Function>(alGetProcAddress(name));
		return function != nullptr;
	}

	bool load_functions() {
		if(m_functionsLoaded)
			return true;

		bool ok = true;
		ok &= load_function<LPALGENEFFECTS>(m_alGenEffects, "alGenEffects");
		ok &= load_function<LPALDELETEEFFECTS>(m_alDeleteEffects, "alDeleteEffects");
		ok &= load_function<LPALEFFECTI>(m_alEffecti, "alEffecti");
		ok &= load_function<LPALEFFECTF>(m_alEffectf, "alEffectf");
		ok &= load_function<LPALGENAUXILIARYEFFECTSLOTS>(m_alGenAuxSlots, "alGenAuxiliaryEffectSlots");
		ok &= load_function<LPALDELETEAUXILIARYEFFECTSLOTS>(m_alDelAuxSlots, "alDeleteAuxiliaryEffectSlots");
		ok &= load_function<LPALAUXILIARYEFFECTSLOTI>(m_alAuxSloti, "alAuxiliaryEffectSloti");
		ok &= load_function<LPALGENFILTERS>(m_alGenFilters, "alGenFilters");
		ok &= load_function<LPALDELETEFILTERS>(m_alDeleteFilters, "alDeleteFilters");
		ok &= load_function<LPALFILTERI>(m_alFilteri, "alFilteri");
		ok &= load_function<LPALFILTERF>(m_alFilterf, "alFilterf");
		m_functionsLoaded = ok;
		return ok;
	}

	static float db_to_gain(float db, float min_gain, float max_gain) {
		const float clampedDb = std::clamp(
			db,
			Settings::min_db,
			Settings::max_db);
		return std::clamp(std::pow(10.0f, clampedDb / 20.0f), min_gain, max_gain);
	}

	void apply_gains(const Settings &settings) {
		m_alEffectf(m_effect, AL_EQUALIZER_LOW_GAIN,
			db_to_gain(settings.low_db, AL_EQUALIZER_MIN_LOW_GAIN, AL_EQUALIZER_MAX_LOW_GAIN));
		m_alEffectf(m_effect, AL_EQUALIZER_MID1_GAIN,
			db_to_gain(settings.mid1_db, AL_EQUALIZER_MIN_MID1_GAIN, AL_EQUALIZER_MAX_MID1_GAIN));
		m_alEffectf(m_effect, AL_EQUALIZER_MID2_GAIN,
			db_to_gain(settings.mid2_db, AL_EQUALIZER_MIN_MID2_GAIN, AL_EQUALIZER_MAX_MID2_GAIN));
		m_alEffectf(m_effect, AL_EQUALIZER_HIGH_GAIN,
			db_to_gain(settings.high_db, AL_EQUALIZER_MIN_HIGH_GAIN, AL_EQUALIZER_MAX_HIGH_GAIN));
		m_alAuxSloti(m_auxSlot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(m_effect));
	}

	void set_enabled(bool enabled) {
		if(enabled) {
			alSource3i(m_source, AL_AUXILIARY_SEND_FILTER,
				static_cast<ALint>(m_auxSlot), 0, AL_FILTER_NULL);
			alSourcei(m_source, AL_DIRECT_FILTER, static_cast<ALint>(m_silentFilter));
		} else {
			alSource3i(m_source, AL_AUXILIARY_SEND_FILTER,
				AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
			alSourcei(m_source, AL_DIRECT_FILTER, AL_FILTER_NULL);
		}
	}

public:
	explicit OpenAlEqualizer(std::atomic<Settings> &settings)
		: m_settings(settings)
	{}

	void open(ALCdevice *device, ALuint source) {
		m_source = source;
		if(!alcIsExtensionPresent(device, "ALC_EXT_EFX")) {
			log_info("ALC_EXT_EFX not available, equalizer disabled");
			return;
		}

		if(!load_functions()) {
			log_error("Failed to load OpenAL EFX functions, equalizer disabled");
			return;
		}

		m_alGenAuxSlots(1, &m_auxSlot);
		m_alGenEffects(1, &m_effect);
		m_alEffecti(m_effect, AL_EFFECT_TYPE, AL_EFFECT_EQUALIZER);
		
		const auto settings = m_settings.load();
		apply_gains(settings);
		m_previousSettings = settings;

		// Mute the direct path while the EQ send is active so the parallel EFX
		// path does not double the volume.
		m_alGenFilters(1, &m_silentFilter);
		m_alFilteri(m_silentFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
		m_alFilterf(m_silentFilter, AL_LOWPASS_GAIN,   0.0f);
		m_alFilterf(m_silentFilter, AL_LOWPASS_GAINHF, 0.0f);

		if(settings.enabled)
			set_enabled(true);
		m_available = true;
		AL_CHECK;
	}

	void update() {
		if(!m_available)
			return;

		const auto settings = m_settings.load();
		if(settings != m_previousSettings) {
			apply_gains(settings);

			if(settings.enabled != m_previousSettings.enabled)
				set_enabled(settings.enabled);
			m_previousSettings = settings;
		}
		AL_CHECK;
	}

	void close() {
		if(!m_available)
			return;

		alSourcei(m_source, AL_DIRECT_FILTER, AL_FILTER_NULL);
		alSource3i(m_source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
		m_alAuxSloti(m_auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
		m_alDelAuxSlots(1, &m_auxSlot);
		m_alDeleteEffects(1, &m_effect);
		m_alDeleteFilters(1, &m_silentFilter);

		m_source = 0;
		m_auxSlot = 0;
		m_effect = 0;
		m_silentFilter = 0;
		m_available = false;
	}
};

} // namespace


enum class State {
	Stopped,
	PlayStart,
	Play,
	PlayEnd,
	Pause,
	Quit
};

const char* state_str(State s) {
	switch(s) {
	case State::Stopped: return "Stopped";
	case State::PlayStart: return "PlayStart";
	case State::Play: return "Play";
	case State::PlayEnd: return "PlayEnd";
	case State::Pause: return "Pause";
	case State::Quit: return "Quit";
	default: return "ERROR";
	}
}


bool player_init(AppState &app) {
	
	const ALuint freq = 48000;



	g_playThread = thread_create("Player", [&]()->void{
		std::mt19937 rng(std::random_device{}());

		SQLITE_CLOSE sqlite3* db = db_open(app.config.database.path);

		xmp_context ctx = xmp_create_context();
		SCOPE_EXIT(
			xmp_free_context(ctx);
		);

		std::string lastPlayed = "";

		ALuint al_fmt = AL_FORMAT_STEREO16;
		int    xmp_fmt = 0;
		std::vector<float> vis_conv_buf; // float conversion buffer for visualizer

		bool                 logAlVersion = true;
		ALCdevice            *alDevice = nullptr;
		ALCcontext           *alContext = nullptr;
		ALuint                alSource  = 0;
		std::array<ALuint, 6> alBuffers = {};
		OpenAlEqualizer equalizer(app.config.player.equalizer);
		
		auto openDev = [&]()->auto {
			alDevice = alcOpenDevice(NULL);
			if(!alDevice) {
				log_error("Failed to open device");
				return State::Stopped;
			}
			
			const ALCint attr[] = {
				ALC_FREQUENCY, freq,
				ALC_HRTF_SOFT, AL_FALSE,
				0
			};
			alContext = alcCreateContext(alDevice, attr);
			if(!alContext) {
				log_error("Failed to create context");
				return State::Stopped;
			}
			
			alcMakeContextCurrent(alContext);
			
			if(logAlVersion) {
				logAlVersion = false;
				
				auto vendor  = alGetString(AL_RENDERER);
				auto version = alGetString(AL_VERSION);
				log_info("OpenAL: {} {}", vendor, version);
			}
			
			
			alGenBuffers(alBuffers.size(), alBuffers.data());
			AL_CHECK;
			alGenSources(1, &alSource);
			AL_CHECK;
			
			alSourcei(alSource, AL_STEREO_MODE_SOFT, AL_SUPER_STEREO_SOFT);
			AL_CHECK;
			alSourcef(alSource, AL_SUPER_STEREO_WIDTH_SOFT, app.config.player.stereo_width.load());
			alListenerf(AL_GAIN, app.config.player.gain.load());
			AL_CHECK;
			alSourcef(alSource, AL_MIN_GAIN, 0.f);
			AL_CHECK;
			alSourcef(alSource, AL_MAX_GAIN, 4.f);
			AL_CHECK;

			equalizer.open(alDevice, alSource);

#ifdef XMP_FORMAT_32BIT
			{
				ALenum fmt32 = alGetEnumValue("AL_FORMAT_STEREO_I32");
				if(fmt32 != 0 && fmt32 != AL_INVALID_VALUE) {
					al_fmt  = static_cast<ALuint>(fmt32);
					xmp_fmt = XMP_FORMAT_32BIT;
					log_info("Using 32-bit audio (XMP_FORMAT_32BIT + AL_FORMAT_STEREO_I32)");
				} else {
					al_fmt  = AL_FORMAT_STEREO16;
					xmp_fmt = 0;
					log_info("AL_FORMAT_STEREO_I32 not available, falling back to 16-bit");
				}
			}
#else
			al_fmt  = AL_FORMAT_STEREO16;
			xmp_fmt = 0;
#endif

			return State::PlayStart;
		};
		
		auto closeDev = [&]()->auto {
			if(!alDevice) return;

			if(alContext) {
				equalizer.close();
				alDeleteSources(1, &alSource);
				AL_CHECK;
				alDeleteBuffers(alBuffers.size(), alBuffers.data());
				AL_CHECK;
				alcMakeContextCurrent(NULL);
			}
			alSource = 0;
			alBuffers.fill(0);
			
			if(alContext)
				alcDestroyContext(alContext);
			alContext = nullptr;
			
			if(alDevice)
				alcCloseDevice(alDevice);
			alDevice = nullptr;
		};
		
		
		auto stopped = [&]()->auto {
			
			if(app.player.request.play) {
				app.player.request.play = false;
				return State::PlayStart;
			}
			
			if(app.player.request.pause) {
				app.player.request.pause = false;
			}
			
			if(app.player.request.playToggle) {
				app.player.request.playToggle = false;
				return State::PlayStart;
			}
			
			if(g_quitRequest) {
				return State::Quit;
			}
			
			sleep_for(100ms);
			return State::Stopped;
		};
		
		bool seekWasRequested = false;
		bool nextWasRequested = false;
		bool prevWasRequested = false;
		bool stopAfterPlayEnd = false;
		float prev_gain          = -1.f;
		float prev_track_gain    = -1.f;
		float prev_stereo_width  = -1.f;
		PlayData pd;
		bool prebuffering = true;
		size_t prebufferCount = 0;
		int lastLoopCount = 0;
		std::optional<int64_t> audibleDuration;
		
		auto playStart = [&]()->auto{
			seekWasRequested = false;
			nextWasRequested = false;
			bool wantPrev = prevWasRequested;
			prevWasRequested = false;
			stopAfterPlayEnd = false;
			prev_gain         = -1.f;
			prev_track_gain   = -1.f;
			prev_stereo_width = -1.f;
			pd = PlayData();
			prebuffering = true;
			prebufferCount = 0;
			lastLoopCount = 0;
			audibleDuration.reset();
			
			
			app.player.request.rating = -1;
			app.player.track.rating = -1;
			app.player.request.trash = false;
			
			
			app.player.request.next = false;

			auto requestedId = app.player.request.playId.get();
			app.player.request.playId.set("");

			auto requestedPlaylistTrackId = app.player.request.playlistTrackId.exchange(0);
			if(requestedPlaylistTrackId != 0) {
				app.player.state.current_playlist_track_id = requestedPlaylistTrackId;
				app.player.state.in_charts_mode = app.player.request.charts_mode.exchange(false);
				app.player.state.current_playing_playlist_id = app.player.request.playlistId.exchange(0);
			}

			FileRow file;

			// Returns the next track ID and updates current_playlist_track_id,
			// or nullopt if playback should stop.
			auto navigate_to_next = [&](int64_t curIdx) -> std::optional<std::string> {
				using Loop = AppState::Player::State::Loop;

				// Charts: index-based navigation (mutex released before DB I/O)
				{
					Guard g(app.charts.nav.mutex);
					if(app.player.state.in_charts_mode && !app.charts.nav.track_ids.empty()) {
						auto& ids = app.charts.nav.track_ids;
						int64_t nextIdx;
						if(app.player.state.shuffle) {
							nextIdx = static_cast<int64_t>(std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng)) + 1;
						} else {
							nextIdx = curIdx + 1;
							if(nextIdx > static_cast<int64_t>(ids.size())) {
								if(app.player.state.loop_status.load() == Loop::Playlist) {
									nextIdx = 1;
								} else {
									app.player.state.current_playlist_track_id
										.compare_exchange_strong(curIdx, int64_t(0));
									return std::nullopt;
								}
							}
						}
						std::string nextId = ids[static_cast<size_t>(nextIdx) - 1];
						if(!app.player.state.current_playlist_track_id
							.compare_exchange_strong(curIdx, nextIdx))
							return std::nullopt;
						return nextId;
					}
				}

				// Static playlist: DB navigation
				std::optional<PlaylistTrackRef> next;
				if(app.player.state.shuffle) {
					next = db_get_random_playlist_track(db, curIdx);
				} else {
					next = db_get_next_playlist_track(db, curIdx);
					if(!next && app.player.state.loop_status.load() == Loop::Playlist) {
						next = db_get_first_playlist_track(db, curIdx);
					}
				}
				if(!next) {
					app.player.state.current_playlist_track_id
						.compare_exchange_strong(curIdx, int64_t(0));
					return std::nullopt;
				}
				if(!app.player.state.current_playlist_track_id
					.compare_exchange_strong(curIdx, next->playlist_track_id))
					return std::nullopt;
				return next->track_id;
			};

			auto navigate_to_prev = [&](int64_t curIdx) -> std::optional<std::string> {
				using Loop = AppState::Player::State::Loop;

				// Charts: index-based navigation
				{
					Guard g(app.charts.nav.mutex);
					if(app.player.state.in_charts_mode && !app.charts.nav.track_ids.empty()) {
						auto& ids = app.charts.nav.track_ids;
						int64_t prevIdx = curIdx - 1;
						if(prevIdx < 1) {
							if(app.player.state.loop_status.load() == Loop::Playlist) {
								prevIdx = static_cast<int64_t>(ids.size());
							} else {
								app.player.state.current_playlist_track_id
									.compare_exchange_strong(curIdx, int64_t(0));
								return std::nullopt;
							}
						}
						std::string prevId = ids[static_cast<size_t>(prevIdx) - 1];
						if(!app.player.state.current_playlist_track_id
							.compare_exchange_strong(curIdx, prevIdx))
							return std::nullopt;
						return prevId;
					}
				}

				// Static playlist: DB navigation
				std::optional<PlaylistTrackRef> prev;
				if(app.player.state.shuffle) {
					prev = db_get_random_playlist_track(db, curIdx);
				} else {
					prev = db_get_prev_playlist_track(db, curIdx);
					if(!prev && app.player.state.loop_status.load() == Loop::Playlist) {
						prev = db_get_last_playlist_track(db, curIdx);
					}
				}
				if(!prev) {
					app.player.state.current_playlist_track_id
						.compare_exchange_strong(curIdx, int64_t(0));
					return std::nullopt;
				}
				if(!app.player.state.current_playlist_track_id
					.compare_exchange_strong(curIdx, prev->playlist_track_id))
					return std::nullopt;
				return prev->track_id;
			};

			if(!requestedId.empty()) {
				db_get_file(db, requestedId, file);
			} else if(app.player.state.loop_status.load() == AppState::Player::State::Loop::Track && !lastPlayed.empty()) {
				db_get_file(db, lastPlayed, file);
			} else {
				auto curIdx = app.player.state.current_playlist_track_id.load();
				if(curIdx != 0) {
					auto nextId = wantPrev ? navigate_to_prev(curIdx) : navigate_to_next(curIdx);
					if(!nextId) return State::Stopped;
					db_get_file(db, *nextId, file);
				} else if(wantPrev && !lastPlayed.empty()) {
					db_get_file(db, lastPlayed, file);
				} else {
					auto id = db_get_random(db);
					if(id) db_get_file(db, *id, file);
				}
			}
			
			auto loudness = db_get_loudness(db, file.id);
			audibleDuration = db_get_audible_duration(db, file.id);
			auto rating = db_get_rating(db, file.id);
			
			pd.id = file.id;

			if(file.rawData.empty()) {
				log_error("Failed to get next track");
				return State::Stopped;
			}

			/* these must be set before loading the module */
			xmp_set_player(ctx, XMP_PLAYER_DEFPAN, 50);

			int r = xmp_load_module_from_memory(ctx, file.rawData.data(), file.rawData.size());
			if (r < 0) {
				log_error("Failed to load Module: {}", xmpu_errstr(r));
				return State::Stopped;
			}

			r = xmp_start_player(ctx, freq, xmp_fmt);
			if (r < 0) {
				log_error("Failed to start player: {}", xmpu_errstr(r));
				return State::Stopped;
			}

			lastPlayed = file.id;
			
			{
				// https://github.com/libxmp/libxmp/blob/master/docs/libxmp.rst#int-xmp_set_playerxmp_context-c-int-param-int-val
				
				int r = 0;
				// Mixing is done by openal
				r = xmp_set_player(ctx, XMP_PLAYER_MIX, 100);
				if(r) {
					log_error("XMP error: {}", xmpu_errstr(r));
				}
				// Best quality resampler
				r = xmp_set_player(ctx, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE);
				if(r) {
					log_error("XMP error: {}", xmpu_errstr(r));
				}
			}
			
			
			struct xmp_module_info mi;
			xmp_get_module_info(ctx, &mi);
			auto name = load_string(mi.mod->name);
			
			auto displayName = is_empty_or_whitespace(name) ? file.name : name;
			app.player.track.id.set(file.id);
			app.player.track.file_name.set(file.name);
			app.player.track.name.set(displayName);
			app.player.track.rating = rating.has_value() ? std::lround(rating.value()) : -1;
			log_debug("Playing: {}", displayName);
			
			app.mpris.request.metadata_changed = true;
			
			
			double trackAlGain =1.0;
			if(loudness.has_value()) {
				auto l = loudness.value();
				auto lDelta = app.config.player.target_loudness - l;
				auto alGain = pow(10.0, lDelta / 20.0);
				log_debug("Loudness Delta: {} AL_GAIN: {}", lDelta, alGain);
				trackAlGain = alGain;
			}
			app.player.track.gain.store(static_cast<float>(trackAlGain));
			
			return State::Play;
		};
		
		auto play = [&]()->auto{
			
			if(!app.player.request.playId.get().empty()) {
				app.player.request.play = false;
				nextWasRequested = true;
				return State::PlayEnd;
			}

			if(app.player.request.play) {
				app.player.request.play = false;
				// We are already Playing
			}
			
			if(app.player.request.pause) {
				app.player.request.pause = false;
				alSourcePause(alSource);
				AL_CHECK;
				return State::Pause;
			}
			
			if(app.player.request.playToggle) {
				app.player.request.playToggle = false;
				alSourcePause(alSource);
				AL_CHECK;
				return State::Pause;
			}
			
			if(app.player.request.stop) {
				app.player.request.stop = false;
				stopAfterPlayEnd = true;
				return State::PlayEnd;
			}
			
			
			if(app.player.request.next) {
				app.player.request.next = false;
				nextWasRequested = true;
				return State::PlayEnd;
			}

			if(app.player.request.prev) {
				app.player.request.prev = false;
				prevWasRequested = true;
				return State::PlayEnd;
			}
			
			if(g_quitRequest) {
				return State::PlayEnd;
			}
			
			
			
			if(xmp_play_frame(ctx) != 0) {
				return State::PlayEnd;
			}
			
			xmp_frame_info fi;
			xmp_get_frame_info(ctx, &fi);

			const bool skipTrailingSilence =
				app.config.player.skip_trailing_silence.load() && audibleDuration.has_value();
			if(skipTrailingSilence && fi.time >= audibleDuration.value()) {
				// This is a natural completion, not a user-requested skip. Leave
				// nextWasRequested clear so play/skip accounting remains correct.
				pd.duration = audibleDuration.value();
				app.player.track.elapsed = static_cast<int>(audibleDuration.value());
				return State::PlayEnd;
			}
			
			if (fi.loop_count > 0) {
				log_debug("Non zero loopcount: {}", fi.loop_count);
			}
			
			/* Check loop */
			if (lastLoopCount != fi.loop_count) {
				return State::PlayEnd;
			}
			lastLoopCount = fi.loop_count;
			// if (fi.loop_count > 0) {
			// 	break;
			// }
			
			app.player.track.length = static_cast<int>(skipTrailingSilence
				? std::min<int64_t>(fi.total_time, audibleDuration.value())
				: fi.total_time);
			app.player.track.elapsed = fi.time;
			
			if(seekWasRequested) {
				seekWasRequested = false;
				app.mpris.request.seek_changed = true;
			}
			
			{
				constexpr int64_t none = std::numeric_limits<int64_t>::min();
				int64_t delta = app.player.request.seek.exchange(none);
				if(delta != none) {
					xmp_seek_time(ctx, fi.time + delta);
					seekWasRequested = true;
				}
				int64_t pos = app.player.request.position.exchange(none);
				if(pos != none) {
					xmp_seek_time(ctx, pos);
					seekWasRequested = true;
				}
			}

			{
				float g = app.config.player.gain.load();
				if(g != prev_gain) {
					prev_gain = g;
					alListenerf(AL_GAIN, g);
					app.mpris.request.gain_changed = true;
				}
			}
			
			{
				float tg = app.player.track.gain.load();
				if(tg != prev_track_gain) {
					prev_track_gain = tg;
					alSourcef(alSource, AL_GAIN, tg);
				}
			}
			{
				float sw = app.config.player.stereo_width.load();
				if(sw != prev_stereo_width) {
					prev_stereo_width = sw;
					alSourcef(alSource, AL_SUPER_STEREO_WIDTH_SOFT, sw);
					AL_CHECK;
				}
			}

			equalizer.update();
			
			pd.duration = fi.time;

			if(fi.time == fi.total_time) {
				log_debug("Completely played");
			}

			{
				const size_t n = fi.buffer_size /
#ifdef XMP_FORMAT_32BIT
					((xmp_fmt & XMP_FORMAT_32BIT) ? sizeof(int32_t) : sizeof(int16_t));
#else
					sizeof(int16_t);
#endif
				vis_conv_buf.resize(n);
#ifdef XMP_FORMAT_32BIT
				if(xmp_fmt & XMP_FORMAT_32BIT) {
					const auto* src = static_cast<const int32_t*>(fi.buffer);
					for(size_t i = 0; i < n; ++i)
						vis_conv_buf[i] = src[i] * (1.0f / 2147483648.0f);
				} else
#endif
				{
					const auto* src = static_cast<const int16_t*>(fi.buffer);
					for(size_t i = 0; i < n; ++i)
						vis_conv_buf[i] = src[i] * (1.0f / 32768.0f);
				}
				app.visualizer.sample_queue.push(vis_conv_buf.data(), n);
			}

			if(prebuffering) {
				alBufferData(alBuffers[prebufferCount], al_fmt, fi.buffer, fi.buffer_size, freq);
				alSourceQueueBuffers(alSource, 1, &alBuffers[prebufferCount]);
				AL_CHECK;
				prebufferCount++;
				
				if(prebufferCount >= alBuffers.size()) {
					prebuffering = false;
					
					ALint val;
					alGetSourcei(alSource, AL_SOURCE_STATE, &val);
					if (val != AL_PLAYING) {
						//alSourcef(g_alSource, AL_GAIN, trackAlGain);
						alSourcePlay(alSource);
					}
				}
			} else {
				ALint val;
				alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &val);
				auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
				while (val <= 0) {
					if (std::chrono::steady_clock::now() > deadline) {
						log_error("AL buffer wait timed out");
						break;
					}
					sleep_for(10ms);
					alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &val);
				}

				alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &val);
				if(val > 0) {
					ALuint buffer;
					alSourceUnqueueBuffers(alSource, 1, &buffer);
					alBufferData(buffer, al_fmt, fi.buffer, fi.buffer_size, freq);
					alSourceQueueBuffers(alSource, 1, &buffer);
					AL_CHECK;
				}
			}
			
			return State::Play;
		};
		
		auto playEnd = [&]()->auto{
			const bool finishQueuedAudio = !nextWasRequested && !prevWasRequested
				&& !stopAfterPlayEnd && !g_quitRequest;

			// Preserve the tail of a naturally completed track. Explicit stop/skip
			// requests discard queued audio immediately.
			bool drainTimedOut = false;
			if(finishQueuedAudio) {
				const auto drainDeadline =
					std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while(!drainTimedOut) {
					ALint buffersQueued;
					alGetSourcei(alSource, AL_BUFFERS_QUEUED, &buffersQueued);
					if(buffersQueued == 0) break;

					ALint processed;
					alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
					if(processed > 0) {
						ALuint buffer;
						alSourceUnqueueBuffers(alSource, 1, &buffer);
						continue;
					}

					if(std::chrono::steady_clock::now() > drainDeadline) {
						log_error("AL buffer drain timed out; forcing source reset");
						drainTimedOut = true;
						break;
					}
					sleep_for(10ms);
				}
			}

			// Stopping makes all queued buffers processed. Always detach every buffer
			// before the next track attempts to refill the fixed buffer array.
			while(alGetError() != AL_NO_ERROR) {}
			alSourceStop(alSource);
			bool sourceResetOk = alGetError() == AL_NO_ERROR;
			ALint buffersQueued = 0;
			if(sourceResetOk) {
				alGetSourcei(alSource, AL_BUFFERS_QUEUED, &buffersQueued);
				sourceResetOk = alGetError() == AL_NO_ERROR;
			}
			while(sourceResetOk && buffersQueued-- > 0) {
				ALuint buffer;
				alSourceUnqueueBuffers(alSource, 1, &buffer);
				sourceResetOk = alGetError() == AL_NO_ERROR;
			}
			if(!sourceResetOk) {
				log_error("Failed to reset OpenAL source queue; recreating audio device");
			}
			
			if(sourceResetOk) {
				alSourcef(alSource, AL_GAIN, 0.f);
			}
			
			xmp_end_player(ctx);
			xmp_release_module(ctx);
			
			// Playback completely done
			
			app.player.track.file_name.set("");
			app.player.track.name.set("");
			app.player.track.length = 0;
			app.player.track.elapsed = 0;
			app.player.track.rating = -1;
			
			
			if(auto r = app.player.request.rating.exchange(-1); r >= 0) {
				pd.rating = r;
			}

			if(app.player.request.trash.exchange(false)) {
				pd.trash = 1;
			}
			
			if(nextWasRequested || prevWasRequested) {
				pd.skipped = 1;
			} else {
				pd.played = 1;
			}
			
			updatePlayback(db, pd);

			if(!sourceResetOk) {
				closeDev();
				if(g_quitRequest || stopAfterPlayEnd) {
					stopAfterPlayEnd = false;
					return State::Stopped;
				}
				return openDev();
			}
			
			if(g_quitRequest) {
				return State::Stopped;
			}
			
			if(stopAfterPlayEnd) {
				stopAfterPlayEnd = false;
				return State::Stopped;
			} else {
				return State::PlayStart;
			}
		};
		
		auto pause = [&]()->auto{
			
			if(!app.player.request.playId.get().empty()) {
				app.player.request.play = false;
				return State::PlayEnd;
			}

			if(app.player.request.play) {
				app.player.request.play = false;
				alSourcePlay(alSource);
				AL_CHECK;
				return State::Play;
			}
			
			if(app.player.request.pause) {
				app.player.request.pause = false;
				// We are already Paused
			}
			
			if(app.player.request.playToggle) {
				app.player.request.playToggle = false;
				alSourcePlay(alSource);
				AL_CHECK;
				return State::Play;
			}

			if(app.player.request.stop) {
				app.player.request.stop = false;
				stopAfterPlayEnd = true;
				return State::PlayEnd;
			}

			if(app.player.request.next) {
				app.player.request.next = false;
				nextWasRequested = true;
				return State::PlayEnd;
			}

			if(app.player.request.prev) {
				app.player.request.prev = false;
				prevWasRequested = true;
				return State::PlayEnd;
			}

			if(g_quitRequest) {
				return State::PlayEnd;
			}

			sleep_for(50ms);
			return State::Pause;
		};
		
		
		auto curr = State::Stopped;
		auto next = State::Stopped;
		while(true) {
			if(curr != next) {
				const auto c = curr;
				const auto n = next;
				curr = next;
				
				log_debug("Player state: {} -> {}", state_str(c), state_str(n));
				
				if(n == State::Stopped) {
					app.player.state.playback_status = AppState::Player::State::Playback::Stop;
				}else if(n == State::Play) {
					app.player.state.playback_status = AppState::Player::State::Playback::Play;
				} else if(n == State::Pause) {
					app.player.state.playback_status = AppState::Player::State::Playback::Pause;
				}
				
				
				if(c == State::Stopped && n == State::PlayStart) {
					next = openDev();
				}
				
				if(n == State::Stopped) {
					closeDev();
				}
			}
			
			
			if(curr == State::Stopped) {
				next = stopped();
			} else if(curr == State::PlayStart) {
				next = playStart();
			} else if(curr == State::Play) {
				next = play();
			} else if(curr == State::PlayEnd) {
				next = playEnd();
			} else if(curr == State::Pause) {
				next = pause();
			} else if(curr == State::Quit) {
				break;
			}
		}
	});
	
	return true;
}

bool player_iterate(AppState &app) {
	
	return true;
}

void player_quit() {
	log_debug("Player shutdown");
	g_quitRequest = true;
	if(g_playThread.joinable())
		g_playThread.join();
	g_quitRequest = false;
}
