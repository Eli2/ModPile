// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <cctype>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string_view trim(std::string_view s) {
	while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
	while (!s.empty() && std::isspace((unsigned char)s.back()))  s.remove_suffix(1);
	return s;
}

static std::string make_key(std::string_view section, std::string_view key) {
	std::string k;
	k.reserve(section.size() + 1 + key.size());
	k.append(section);
	k += '.';
	k.append(key);
	return k;
}

// ─── TomlReader ──────────────────────────────────────────────────────────────

bool TomlReader::load(const std::filesystem::path &path) {
	std::ifstream f(path);
	if (!f) return false;

	std::string section;
	std::string line;
	while (std::getline(f, line)) {
		std::string_view sv = trim(line);

		// Skip blank lines and comments
		if (sv.empty() || sv[0] == '#') continue;

		// Section header [name]
		if (sv[0] == '[') {
			auto close = sv.find(']');
			if (close == std::string_view::npos) continue;
			section = std::string(trim(sv.substr(1, close - 1)));
			continue;
		}

		// key = value
		auto eq = sv.find('=');
		if (eq == std::string_view::npos) continue;
		std::string key(trim(sv.substr(0, eq)));
		std::string_view raw = trim(sv.substr(eq + 1));

		if (key.empty() || section.empty()) continue;

		TomlValue val;

		// String: "..."
		if (raw.size() >= 2 && raw.front() == '"') {
			// Strip surrounding quotes; handle \" escape
			std::string s;
			for (size_t i = 1; i < raw.size() - 1; ++i) {
				if (raw[i] == '\\' && i + 1 < raw.size() - 1) {
					char c = raw[++i];
					switch (c) {
						case '"':  s += '"';  break;
						case '\\': s += '\\'; break;
						case 'n':  s += '\n'; break;
						case 't':  s += '\t'; break;
						default:   s += '\\'; s += c; break;
					}
				} else {
					s += raw[i];
				}
			}
			val.type = TomlValue::Type::String;
			val.str  = std::move(s);

		// Bool
		} else if (raw == "true") {
			val.type = TomlValue::Type::Bool;
			val.b    = true;
		} else if (raw == "false") {
			val.type = TomlValue::Type::Bool;
			val.b    = false;

		// Hex integer: 0x…
		} else if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
			try {
				val.type = TomlValue::Type::Integer;
				val.i    = static_cast<int64_t>(std::stoull(std::string(raw), nullptr, 16));
			} catch (...) { continue; }

		// Float (contains '.' or 'e'/'E')
		} else if (raw.find('.') != std::string_view::npos ||
		           raw.find('e') != std::string_view::npos ||
		           raw.find('E') != std::string_view::npos) {
			try {
				val.type = TomlValue::Type::Float;
				val.f    = std::stod(std::string(raw));
			} catch (...) { continue; }

		// Integer
		} else {
			try {
				val.type = TomlValue::Type::Integer;
				val.i    = std::stoll(std::string(raw));
			} catch (...) { continue; }
		}

		m_values[make_key(section, key)] = val;
	}
	return true;
}

const TomlValue *TomlReader::find(std::string_view section, std::string_view key) const {
	auto it = m_values.find(make_key(section, key));
	if (it == m_values.end()) return nullptr;
	return &it->second;
}

std::optional<std::string> TomlReader::get_string(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::String) return std::nullopt;
	return v->str;
}

std::optional<int64_t> TomlReader::get_integer(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::Integer) return std::nullopt;
	return v->i;
}

std::optional<double> TomlReader::get_float(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::Float) return std::nullopt;
	return v->f;
}

std::optional<bool> TomlReader::get_bool(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::Bool) return std::nullopt;
	return v->b;
}

// ─── TomlWriter ──────────────────────────────────────────────────────────────

void TomlWriter::section(std::string_view name) {
	if (!m_first_section) m_buf += '\n';
	m_first_section = false;
	m_buf += '[';
	m_buf.append(name);
	m_buf += "]\n";
}

static std::string escape_string(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 2);
	out += '"';
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
	out += '"';
	return out;
}

void TomlWriter::write(std::string_view key, std::string_view value) {
	m_buf.append(key);
	m_buf += " = ";
	m_buf += escape_string(value);
	m_buf += '\n';
}

void TomlWriter::write(std::string_view key, int64_t value) {
	m_buf.append(key);
	m_buf += " = ";
	m_buf += std::to_string(value);
	m_buf += '\n';
}

void TomlWriter::write_hex(std::string_view key, int64_t value) {
	m_buf.append(key);
	m_buf += std::format(" = 0x{:04x}\n", static_cast<uint64_t>(value));
}

void TomlWriter::write(std::string_view key, double value) {
	m_buf.append(key);
	// Always emit a decimal point so the parser round-trips as float.
	m_buf += std::format(" = {:.7g}\n", value);
}

void TomlWriter::write(std::string_view key, bool value) {
	m_buf.append(key);
	m_buf += value ? " = true\n" : " = false\n";
}

bool TomlWriter::save(const std::filesystem::path &path) const {
	std::ofstream f(path, std::ios::out | std::ios::binary);
	if (!f) return false;
	f.write(m_buf.data(), static_cast<std::streamsize>(m_buf.size()));
	return f.good();
}
