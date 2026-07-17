// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "export.h"

#include <fstream>
#include <set>
#include <sstream>

#include "../db_common.h"
#include "../log.h"

namespace {

static std::string xml_escape(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	for(char c : s) {
		switch(c) {
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default:   out += c;        break;
		}
	}
	return out;
}

static std::string uri_encode(const std::string &s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size());
	for(unsigned char c : s) {
		// unreserved URI characters: ALPHA / DIGIT / "-" / "." / "_" / "~"
		if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		   (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		   c == '_' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// Returns a unique filename within the used_names set.
// If the base name collides, appends the first 8 chars of the track id.
static std::string unique_filename(
	const std::string &file_name,
	const std::string &id,
	std::set<std::string> &used_names)
{
	// Use only the basename, stripping any directory component
	std::filesystem::path p(file_name);
	std::string base = p.filename().string();
	if(base.empty()) base = id.substr(0, 16);

	if(used_names.find(base) == used_names.end()) {
		used_names.insert(base);
		return base;
	}

	// Collision: insert id prefix before the extension
	std::filesystem::path fp(base);
	std::string stem = fp.stem().string();
	std::string ext  = fp.extension().string();
	std::string candidate = stem + "_" + id.substr(0, 8) + ext;
	used_names.insert(candidate);
	return candidate;
}

} // namespace

bool export_playlist_run(
	TaskControl &tc,
	sqlite3 *db,
	const std::filesystem::path &directory,
	const std::string &playlist_name,
	const std::vector<ExportTrack> &tracks)
{
	auto task_status = tc.scope(std::format("Exporting playlist \"{}\"", playlist_name));

	std::set<std::string> used_names;
	// Mapping from track index to the filename written on disk
	std::vector<std::string> written_names(tracks.size());

	for(size_t i = 0; i < tracks.size(); ++i) {
		if(tc.abort) return false;

		const auto &t = tracks[i];
		task_status.progress(i + 1, tracks.size(), "tracks");
		auto track_status = tc.scope(t.file_name);

		FileRow file;
		if(!db_get_file(db, t.id, file)) {
			const auto message = std::format("Failed to read track {} for export.", t.id);
			log_error("{}", message);
			tc.fail(message);
			return false;
		}

		std::string fname = unique_filename(t.file_name, t.id, used_names);
		written_names[i] = fname;

		auto dest = directory / fname;
		std::ofstream out(dest, std::ios::binary);
		if(!out) {
			const auto message = std::format("Failed to open {} for writing.", dest.string());
			log_error("{}", message);
			tc.fail(message);
			return false;
		}
		out.write(reinterpret_cast<const char*>(file.rawData.data()), static_cast<std::streamsize>(file.rawData.size()));
		out.close();
		if(!out) {
			const auto message = std::format("Failed to write {}.", dest.string());
			log_error("{}", message);
			tc.fail(message);
			return false;
		}
	}

	// Write XSPF
	task_status.clear_progress();
	auto xspf_status = tc.scope("Writing XSPF");
	std::ostringstream xspf;
	xspf << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	xspf << "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n";
	xspf << "\t<trackList>\n";
	for(size_t i = 0; i < tracks.size(); ++i) {
		if(written_names[i].empty()) {
			continue;
		}
		const auto &t = tracks[i];
		xspf << "\t\t<track>\n";
		xspf << "\t\t\t<location>" << uri_encode(written_names[i]) << "</location>\n";
		if(!t.name.empty())
			xspf << "\t\t\t<title>" << xml_escape(t.name) << "</title>\n";
		if(!t.artist.empty())
			xspf << "\t\t\t<creator>" << xml_escape(t.artist) << "</creator>\n";
		xspf << "\t\t</track>\n";
	}
	xspf << "\t</trackList>\n";
	xspf << "</playlist>\n";

	// Sanitize playlist name for use as filename
	std::string xspf_name = playlist_name;
	for(char &c : xspf_name) {
		if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
		   c == '"' || c == '<'  || c == '>' || c == '|') {
			c = '_';
		}
	}
	if(xspf_name.empty()) xspf_name = "playlist";

	auto xspf_path = directory / (xspf_name + ".xspf");
	std::ofstream xf(xspf_path);
	if(!xf) {
		const auto message = std::format("Failed to open {} for writing.", xspf_path.string());
		log_error("{}", message);
		tc.fail(message);
		return false;
	}
	xf << xspf.str();
	xf.close();
	if(!xf) {
		const auto message = std::format("Failed to write {}.", xspf_path.string());
		log_error("{}", message);
		tc.fail(message);
		return false;
	}
	return true;
}
