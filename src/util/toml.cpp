// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <cctype>
#include <cstdio>
#include <format>
#include <fstream>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string_view trim(std::string_view s) {
	while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
	while (!s.empty() && std::isspace((unsigned char)s.back()))  s.remove_suffix(1);
	return s;
}

static std::string_view strip_comment(std::string_view s) {
	bool in_string = false;
	bool escaped = false;
	for (size_t i = 0; i < s.size(); ++i) {
		if (in_string && s[i] == '\\' && !escaped) {
			escaped = true;
			continue;
		}
		if (s[i] == '"' && !escaped) {
			in_string = !in_string;
		} else if (s[i] == '#' && !in_string) {
			return trim(s.substr(0, i));
		}
		escaped = false;
	}
	return trim(s);
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
	return load(f);
}

bool TomlReader::load(std::istream &input) {
	m_values.clear();
	std::string section;
	std::string line;
	while (std::getline(input, line)) {
		std::string_view sv = strip_comment(line);

		// Skip blank lines and comments
		if (sv.empty() || sv[0] == '#') {
			continue;
		}

		// Section header [name]
		if (sv[0] == '[') {
			auto close = sv.find(']');
			if (close == std::string_view::npos) {
				continue;
			}
			section = std::string(trim(sv.substr(1, close - 1)));
			continue;
		}

		// key = value
		auto eq = sv.find('=');
		if (eq == std::string_view::npos) {
			continue;
		}
		std::string key(trim(sv.substr(0, eq)));
		std::string_view raw = trim(sv.substr(eq + 1));
		
		if (key.empty() || section.empty()) {
			continue;
		}

		TomlValue val;

		// String: "..."
		if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
			// Strip surrounding quotes; handle \" escape
			std::string s;
			bool valid = true;
			for (size_t i = 1; i < raw.size() - 1; ++i) {
				if (raw[i] == '\\' && i + 1 < raw.size() - 1) {
					char c = raw[++i];
					switch (c) {
						case 'b':  s += '\b'; break;
						case 'f':  s += '\f'; break;
						case '"':  s += '"';  break;
						case '\\': s += '\\'; break;
						case 'n':  s += '\n'; break;
						case 'r':  s += '\r'; break;
						case 't':  s += '\t'; break;
						default:   valid = false; break;
					}
					if (!valid) break;
				} else {
					s += raw[i];
				}
			}
			if (!valid) continue;
			val.type = TomlValue::Type::String;
			val.str  = std::move(s);
		} else if (!raw.empty() && raw.front() == '"') {
			continue;

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
				size_t parsed = 0;
				auto value = std::stoull(std::string(raw), &parsed, 16);
				if (parsed != raw.size() || value > static_cast<uint64_t>(INT64_MAX)) continue;
				val.type = TomlValue::Type::Integer;
				val.i    = static_cast<int64_t>(value);
			} catch (...) {
				continue;
			}

		// Float (contains '.' or 'e'/'E')
		} else if (raw.find('.') != std::string_view::npos ||
		           raw.find('e') != std::string_view::npos ||
		           raw.find('E') != std::string_view::npos) {
			try {
				size_t parsed = 0;
				auto value = std::stod(std::string(raw), &parsed);
				if (parsed != raw.size()) continue;
				val.type = TomlValue::Type::Float;
				val.f    = value;
			} catch (...) {
				continue;
			}

		// Integer
		} else {
			try {
				size_t parsed = 0;
				auto value = std::stoll(std::string(raw), &parsed);
				if (parsed != raw.size()) continue;
				val.type = TomlValue::Type::Integer;
				val.i    = value;
			} catch (...) {
				continue;
			}
		}

		m_values[make_key(section, key)] = val;
	}
	return !input.bad();
}

const TomlValue *TomlReader::find(std::string_view section, std::string_view key) const {
	auto it = m_values.find(make_key(section, key));
	if (it == m_values.end()) {
		return nullptr;
	}
	return &it->second;
}

std::optional<std::string> TomlReader::get_string(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::String) {
		return std::nullopt;
	}
	return v->str;
}

std::optional<int64_t> TomlReader::get_integer(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::Integer) {
		return std::nullopt;
	}
	return v->i;
}

std::optional<double> TomlReader::get_float(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v) {
		return std::nullopt;
	}
	if (v->type == TomlValue::Type::Float) {
		return v->f;
	}
	if (v->type == TomlValue::Type::Integer) {
		return static_cast<double>(v->i);
	}
	return std::nullopt;
}

std::optional<bool> TomlReader::get_bool(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || v->type != TomlValue::Type::Bool) {
		return std::nullopt;
	}
	return v->b;
}

// ─── TomlWriter ──────────────────────────────────────────────────────────────

static std::string_view newline_for(std::string_view document) {
	return document.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
}

static size_t find_unquoted(std::string_view text, char needle, size_t start = 0) {
	bool in_basic_string = false;
	bool in_literal_string = false;
	bool escaped = false;
	for (size_t i = start; i < text.size(); ++i) {
		const char c = text[i];
		if (in_basic_string && c == '\\' && !escaped) {
			escaped = true;
			continue;
		}
		if (c == '"' && !in_literal_string && !escaped) {
			in_basic_string = !in_basic_string;
		} else if (c == '\'' && !in_basic_string) {
			in_literal_string = !in_literal_string;
		} else if (c == needle && !in_basic_string && !in_literal_string) {
			return i;
		}
		escaped = false;
	}
	return std::string_view::npos;
}

static std::optional<std::string_view> section_name(std::string_view line) {
	const auto comment = find_unquoted(line, '#');
	if (comment != std::string_view::npos) line = line.substr(0, comment);
	line = trim(line);
	if (line.size() < 2 || line.front() != '[' || line.back() != ']' ||
	    (line.size() >= 2 && line[1] == '[')) {
		return std::nullopt;
	}
	return trim(line.substr(1, line.size() - 2));
}

bool TomlWriter::load(const std::filesystem::path &path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input) return false;
	return load(input);
}

bool TomlWriter::load(std::istream &input) {
	std::string document;
	char buffer[4096];
	while (input) {
		input.read(buffer, sizeof(buffer));
		const auto count = input.gcount();
		if (count > 0) document.append(buffer, static_cast<size_t>(count));
	}
	if (input.bad()) return false;
	m_buf = std::move(document);
	m_section.clear();
	return true;
}

void TomlWriter::section(std::string_view name) {
	m_section = name;

	for (size_t begin = 0; begin < m_buf.size();) {
		const auto newline = m_buf.find('\n', begin);
		const auto end = newline == std::string::npos ? m_buf.size() : newline;
		auto line = std::string_view(m_buf).substr(begin, end - begin);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		if (auto existing = section_name(line); existing && *existing == name) return;
		begin = newline == std::string::npos ? m_buf.size() : newline + 1;
	}

	const auto newline = newline_for(m_buf);
	if (!m_buf.empty() && m_buf.back() != '\n') m_buf.append(newline);
	if (!m_buf.empty()) m_buf.append(newline);
	m_buf += '[';
	m_buf.append(name);
	m_buf += ']';
	m_buf.append(newline);
}

static std::string escape_string(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 2);
	out += '"';
	for (char c : s) {
		switch (c) {
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
	out += '"';
	return out;
}

void TomlWriter::write(std::string_view key, std::string_view value) {
	write_value(key, escape_string(value));
}

void TomlWriter::write(std::string_view key, int64_t value) {
	write_value(key, std::to_string(value));
}

void TomlWriter::write_hex(std::string_view key, int64_t value) {
	write_value(key, std::format("0x{:04x}", static_cast<uint64_t>(value)));
}

void TomlWriter::write(std::string_view key, double value) {
	// Integral-looking output is accepted by get_float() as well.
	write_value(key, std::format("{:.7g}", value));
}

void TomlWriter::write(std::string_view key, bool value) {
	write_value(key, value ? "true" : "false");
}

void TomlWriter::write_value(std::string_view key, std::string value) {
	std::string current_section;
	size_t insertion = m_buf.size();
	bool found_section = false;

	for (size_t begin = 0; begin < m_buf.size();) {
		const auto newline_pos = m_buf.find('\n', begin);
		const auto physical_end = newline_pos == std::string::npos ? m_buf.size() : newline_pos;
		const auto next_begin = newline_pos == std::string::npos ? m_buf.size() : newline_pos + 1;
		size_t content_end = physical_end;
		if (content_end > begin && m_buf[content_end - 1] == '\r') --content_end;
		auto line = std::string_view(m_buf).substr(begin, content_end - begin);

		if (auto section = section_name(line)) {
			if (found_section && *section != m_section) {
				break;
			}
			current_section = *section;
			found_section = current_section == m_section;
			if (found_section) insertion = next_begin;
		} else if (found_section) {
			const auto equals = find_unquoted(line, '=');
			if (equals != std::string_view::npos && trim(line.substr(0, equals)) == key) {
				size_t value_begin = equals + 1;
				while (value_begin < line.size() &&
				       std::isspace(static_cast<unsigned char>(line[value_begin]))) ++value_begin;
				auto comment = find_unquoted(line, '#', value_begin);
				size_t value_end = comment == std::string_view::npos ? line.size() : comment;
				while (value_end > value_begin &&
				       std::isspace(static_cast<unsigned char>(line[value_end - 1]))) --value_end;
				m_buf.replace(begin + value_begin, value_end - value_begin, value);
				return;
			}
			if (!strip_comment(line).empty()) insertion = next_begin;
		}

		begin = next_begin;
	}

	const auto newline = newline_for(m_buf);
	std::string assignment;
	assignment.reserve(key.size() + value.size() + newline.size() + 3);
	assignment.append(key);
	assignment += " = ";
	assignment += value;
	assignment.append(newline);
	if (insertion == m_buf.size() && !m_buf.empty() && m_buf.back() != '\n') {
		assignment.insert(0, newline);
	}
	m_buf.insert(insertion, assignment);
}

bool TomlWriter::save(const std::filesystem::path &path) const {
	std::ofstream f(path, std::ios::out | std::ios::binary);
	if (!f) {
		return false;
	}
	return save(f);
}

bool TomlWriter::save(std::ostream &output) const {
	output.write(m_buf.data(), static_cast<std::streamsize>(m_buf.size()));
	return output.good();
}
