// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <format>
#include "mpris.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <systemd/sd-bus.h>
#include <vector>


static int method_noop(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int get_false(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	// D-Bus booleans are 32 bits
	uint32_t value = 0;
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &value);
	if(r < 0)
		return r;
	
	return 0;
}

static int get_true(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	// D-Bus booleans are 32 bits
	uint32_t value = 1;
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &value);
	if(r < 0)
		return r;
	
	return 0;
}

static int get_double_one(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	double value = 1.0;
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_DOUBLE, &value);
	if(r < 0)
		return r;
	
	return 0;
}

static int set_nop(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *value,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	
	r = sd_bus_reply_method_return(value, "");
	if(r < 0)
		return r;
	
	return 0;
}


#define METHOD(_member, _signature, _handler) \
SD_BUS_METHOD(_member, _signature, "", _handler, 0)

#define PROP_READ(_name, _type, _read) \
SD_BUS_PROPERTY(_name, _type, _read, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

#define PROP_NOEMIT(_name, _type, _read) \
SD_BUS_PROPERTY(_name, _type, _read, 0, 0)

#define PROP_WRITE(_name, _type, _read, _write) \
SD_BUS_WRITABLE_PROPERTY(_name, _type, _read, _write, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

#define PROP_CONST(_name, _type, _read) \
SD_BUS_PROPERTY(_name, _type, _read, 0, SD_BUS_VTABLE_PROPERTY_CONST)

using PlaybackState = AppState::Player::State::Playback;
using LoopState = AppState::Player::State::Loop;

namespace MediaPlayer2 {

static int Quit(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	app.request.quit = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Identity(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	auto id = "ModPile";
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, id);
	if(r < 0)
		return r;
	
	return 0;
}

static int SupportedUriSchemes(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	static const char * const schemes[] = {
		"file", "http", "ftp", nullptr
	};
	
	r = sd_bus_message_append_strv(reply, (char **)schemes);
	if(r < 0)
		return r;
	
	return 0;
}

static int SupportedMimeTypes(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	static const char * const types[] = { nullptr };
	
	r = sd_bus_message_append_strv(reply, (char **)types);
	if(r < 0)
		return r;
	
	return 0;
}

static const auto interface = "org.mpris.MediaPlayer2";
static const sd_bus_vtable vtable[] = {
	SD_BUS_VTABLE_START(0),
	// Methods
	METHOD("Raise",                    "",  method_noop),
	METHOD("Quit",                     "",  Quit),
	// Properties
	PROP_CONST("CanQuit",             "b",  get_true),
	PROP_WRITE("Fullscreen",          "b",  get_false, set_nop),
	PROP_CONST("CanSetFullscreen",    "b",  get_false),
	PROP_CONST("CanRaise",            "b",  get_false), // XXX Does not work on modern desktops
	PROP_CONST("HasTrackList",        "b",  get_false),
	PROP_CONST("Identity",            "s",  Identity),
	// XXX "DesktopEntry"
	PROP_CONST("SupportedUriSchemes", "as", SupportedUriSchemes),
	PROP_CONST("SupportedMimeTypes",  "as", SupportedMimeTypes),
	SD_BUS_VTABLE_END,
};

} // MediaPlayer2

namespace MediaPlayer2::Player {

static int Next(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.next = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Previous(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.prev = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Pause(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.pause = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int PlayPause(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.playToggle = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Stop(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.stop = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Play(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	app.player.request.play = true;
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int Seek(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	int64_t val = 0;
	r = sd_bus_message_read_basic(m, SD_BUS_TYPE_INT64, &val);
	if(r < 0)
		return r;
	
	app.player.request.seek.store(val / 1000);
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int SetPosition(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	auto currId = app.player.track.id.get();
	auto currPath = std::format("/{}", currId);
	
	const char *path = nullptr;
	r = sd_bus_message_read_basic(m, SD_BUS_TYPE_OBJECT_PATH, &path);
	if(r < 0)
		return r;
	
	int64_t val = 0;
	r = sd_bus_message_read_basic(m, SD_BUS_TYPE_INT64, &val);
	if(r < 0)
		return r;
	
	// If mismatch we got an old request
	if(std::string(path) == currPath) {
		app.player.request.position.store(val / 1000);
	}
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int OpenUri(
	sd_bus_message *m,
	void *userdata,
	sd_bus_error *ret_error
) {
	int r;
	
	const char *path = nullptr;
	r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &path);
	if(r < 0)
		return r;
	
	// TODO implement
	
	r = sd_bus_reply_method_return(m, "");
	if(r < 0)
		return r;
	
	return 0;
}

// ====================================
// Properties

static int get_PlaybackStatus(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	auto str = [](PlaybackState s) {
		switch (s) {
			default:
			case PlaybackState::Stop:  return "Stopped";
			case PlaybackState::Play:  return "Playing";
			case PlaybackState::Pause: return "Paused";
		}
	}(app.player.state.playback_status);
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, str);
	if(r < 0)
		return r;
	
	return 0;
}

static int get_LoopStatus(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	auto str = [](LoopState s) {
		switch (s) {
			default:
			case LoopState::None:     return "None";
			case LoopState::Track:    return "Track";
			case LoopState::Playlist: return "Playlist";
		}
	}(app.player.state.loop_status);
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, str);
	if(r < 0)
		return r;
	
	return 0;
}

static int set_LoopStatus(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *value,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	const char *t = nullptr;
	r = sd_bus_message_read_basic(value, SD_BUS_TYPE_STRING, &t);
	if(r < 0)
		return r;
	
	std::string s = (t);
	if(s == "None") {
		app.player.state.loop_status = LoopState::None;
	} else if(s == "Track") {
		app.player.state.loop_status = LoopState::Track;
	} else if(s == "Playlist") {
		app.player.state.loop_status = LoopState::Playlist;
	}
	
	r = sd_bus_reply_method_return(value, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int get_Shuffle(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;

	const uint32_t s = app.player.state.shuffle.load() ? 1 : 0;
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &s);
	if(r < 0)
		return r;

	return 0;
}

static int set_Shuffle(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *value,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;

	uint32_t s = 0;
	r = sd_bus_message_read_basic(value, SD_BUS_TYPE_BOOLEAN, &s);
	if(r < 0)
		return r;

	app.player.state.shuffle = (s != 0);

	r = sd_bus_reply_method_return(value, "");
	if(r < 0)
		return r;

	return 0;
}

static int get_Volume(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	double vol = app.player.state.gain;
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_DOUBLE, &vol);
	if(r < 0)
		return r;
	
	return 0;
}

static int set_Volume(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *value,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	double vol = 0;
	r = sd_bus_message_read_basic(value, SD_BUS_TYPE_DOUBLE, &vol);
	if(r < 0)
		return r;
	
	vol = std::clamp(vol, 0.0, 1.0);
	
	app.player.state.gain.store(static_cast<float>(vol));
	
	r = sd_bus_reply_method_return(value, "");
	if(r < 0)
		return r;
	
	return 0;
}

static int get_Position(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	int64_t pos = app.player.track.elapsed;
	pos *= 1000;
	
	r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_INT64, &pos);
	if(r < 0)
		return r;
	
	return 0;
}

static int append_key_value(
	sd_bus_message *m,
	const char *key,
	char type,
	const void *value
) {
	int r;
	
	r = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
	if(r < 0)
		return r;
	
	r = sd_bus_message_append_basic(m, SD_BUS_TYPE_STRING, key);
	if(r < 0)
		return r;
	
	{
		const char contents[] = { type, 0 };
		r = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, contents);
		if(r < 0)
			return r;
		
		r = sd_bus_message_append_basic(m, type, value);
		if(r < 0)
			return r;
		
		r = sd_bus_message_close_container(m);
		if(r < 0)
			return r;
	}
	
	r = sd_bus_message_close_container(m);
	if(r < 0)
		return r;
	
	return 0;
}

static int append_key_path(sd_bus_message *m, const char *key, const char *b)
{
	return append_key_value(m, key, SD_BUS_TYPE_OBJECT_PATH, b);
}

static int append_key_int64(sd_bus_message *m, const char *key, int64_t value)
{
	return append_key_value(m, key, SD_BUS_TYPE_INT64, &value);
}

static int append_key_string(sd_bus_message *m, const char *key, const char *b)
{
	return append_key_value(m, key, SD_BUS_TYPE_STRING, b);
}

static int get_Metadata(
	sd_bus *bus,
	const char *path,
	const char *interface,
	const char *property,
	sd_bus_message *reply,
	void *userdata,
	sd_bus_error *ret_error
) {
	auto &app = *static_cast<AppState*>(userdata);
	int r;
	
	r = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	if(r < 0)
		return r;
	
	std::string id = app.player.track.id.get();
	if(!id.empty()) {
		std::string foo = std::format("/{}", id);
		r = append_key_path(reply, "mpris:trackid", foo.c_str());
		if(r < 0)
			return r;
		
		long duration = app.player.track.length;
		duration *= 1000;
		r = append_key_int64(reply, "mpris:length", duration);
		if(r < 0)
			return r;
		
		// auto img = "data:image/gif;base64,R0lGODdhAQABAPAAAP8AAAAAACwAAAAAAQABAAACAkQBADs=";
		// r = mpris_msg_append_ss_dict(reply, "mpris:artUrl", img);
		// if(r < 0)
		// 	return r;
		
		std::string s = app.player.track.name.get();
		r = append_key_string(reply, "xesam:title", s.c_str());
		if(r < 0)
			return r;
	}
	
	r = sd_bus_message_close_container(reply);
	if(r < 0)
		return r;
	
	return 0;
}

static const auto interface = "org.mpris.MediaPlayer2.Player";

static const sd_bus_vtable vtable[] = {
	SD_BUS_VTABLE_START(0),
	// Methods
	METHOD("Next",          "",      Next),
	METHOD("Previous",      "",      Previous),
	METHOD("Pause",         "",      Pause),
	METHOD("PlayPause",     "",      PlayPause),
	METHOD("Stop",          "",      Stop),
	METHOD("Play",          "",      Play),
	METHOD("Seek",          "x",     Seek),
	METHOD("SetPosition",   "ox",    SetPosition),
	METHOD("OpenUri",       "s",     OpenUri),
	// Signals
	SD_BUS_SIGNAL("Seeked",       "x", 0),
	// Properties
	PROP_READ  ("PlaybackStatus",  "s",     get_PlaybackStatus),
	PROP_WRITE ("LoopStatus",      "s",     get_LoopStatus, set_LoopStatus),
	PROP_WRITE ("Rate",            "d",     get_double_one, set_nop),
	PROP_WRITE ("Shuffle",         "b",     get_Shuffle,    set_Shuffle),
	PROP_READ  ("Metadata",        "a{sv}", get_Metadata),
	PROP_WRITE ("Volume",          "d",     get_Volume,     set_Volume),
	PROP_NOEMIT("Position",        "x",     get_Position),
	PROP_CONST ("MinimumRate",     "d",     get_double_one),
	PROP_CONST ("MaximumRate",     "d",     get_double_one),
	PROP_CONST ("CanGoNext",       "b",     get_true),
	PROP_CONST ("CanGoPrevious",   "b",     get_true),
	PROP_CONST ("CanPlay",         "b",     get_true),
	PROP_CONST ("CanPause",        "b",     get_true),
	PROP_CONST ("CanSeek",         "b",     get_true),
	PROP_CONST ("CanControl",      "b",     get_true),
	SD_BUS_VTABLE_END,
};

} // MediaPlayer2::Player

#undef METHOD
#undef PROP_READ
#undef PROP_NOEMIT
#undef PROP_WRITE
#undef PROP_CONST

static sd_bus *g_dbus;

static const auto path = "/org/mpris/MediaPlayer2";

void mpris_seeked(AppState &app)
{
	int64_t pos = app.player.track.elapsed;
	pos *= 1000;
	
	sd_bus_emit_signal(
		g_dbus,
		path,
		MediaPlayer2::Player::interface,
		"Seeked",
		"x",
		pos
	);
}

void mpris_init(AppState &app)
{
	int r;
	
	r = sd_bus_default_user(&g_dbus);
	if(r < 0) {
		log_error("MPRIS error {}", strerror(-r));
		sd_bus_unref(g_dbus);
		g_dbus = nullptr;
		return;
	}
	
	r = sd_bus_add_object_vtable(
		g_dbus, nullptr, path,
		MediaPlayer2::interface,
		MediaPlayer2::vtable,
		&app
	);
	if(r < 0) {
		log_error("MPRIS error {}", strerror(-r));
		sd_bus_unref(g_dbus);
		g_dbus = nullptr;
		return;
	}
	
	r = sd_bus_add_object_vtable(
		g_dbus, nullptr, path,
		MediaPlayer2::Player::interface,
		MediaPlayer2::Player::vtable,
		&app
	);
	if(r < 0) {
		log_error("MPRIS error {}", strerror(-r));
		sd_bus_unref(g_dbus);
		g_dbus = nullptr;
		return;
	}
	
	r = sd_bus_request_name(g_dbus, "org.mpris.MediaPlayer2.ModPile", 0);
	if(r < 0) {
		log_error("MPRIS error {}", strerror(-r));
		sd_bus_unref(g_dbus);
		g_dbus = nullptr;
		return;
	}
}

static PlaybackState last_playback_status;
static LoopState last_loop_state;
static bool last_shuffle = false;

static std::vector<const char*> changed;

void mpris_iterate(AppState &app)
{
	if (!g_dbus) {
		return;
	}
	
	while(true) {
		auto r = sd_bus_process(g_dbus, nullptr);
		if(r < 0) {
			log_error("MPRIS error {}", strerror(-r));
			break;
		}
		if(r == 0) {
			break;
		}
	}
	
	changed.clear();
	
	if(app.mpris.request.seek_changed) {
		app.mpris.request.seek_changed = false;
		
		mpris_seeked(app);
	}
	
	PlaybackState ps = app.player.state.playback_status;
	if(last_playback_status != ps) {
		last_playback_status = ps;
		
		changed.push_back("PlaybackStatus");
	}
	
	LoopState ls = app.player.state.loop_status;
	if(last_loop_state != ls) {
		last_loop_state = ls;
		
		changed.push_back("LoopStatus");
	}
	
	bool sh = app.player.state.shuffle.load();
	if(last_shuffle != sh) {
		last_shuffle = sh;
		changed.push_back("Shuffle");
	}

	if(app.mpris.request.metadata_changed) {
		app.mpris.request.metadata_changed = false;
		
		changed.push_back("Metadata");
	}
	
	if(app.mpris.request.gain_changed) {
		app.mpris.request.gain_changed = false;
		
		changed.push_back("Volume");
	}
	
	if(!changed.empty()) {
		changed.push_back(nullptr);
		
		sd_bus_emit_properties_changed_strv(
			g_dbus,
			path,
			MediaPlayer2::Player::interface,
			const_cast<char **>(changed.data())
		);
	}
}

void mpris_quit()
{
	sd_bus_flush_close_unref(g_dbus);
	g_dbus = nullptr;
}
