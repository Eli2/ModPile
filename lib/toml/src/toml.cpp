// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <algorithm>
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
	m_sections.clear();
	m_entries.clear();
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
			if (!section.empty() &&
			    std::find(m_sections.begin(), m_sections.end(), section) == m_sections.end()) {
				m_sections.push_back(section);
			}
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
		m_entries.push_back({section, key, std::move(val)});
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

bool TomlReader::get(
	std::string &value,
	std::string_view section,
	std::string_view key) const
{
	auto parsed = get_string(section, key);
	if(!parsed)
		return false;
	value = std::move(*parsed);
	return true;
}

bool TomlReader::get(
	std::filesystem::path &value,
	std::string_view section,
	std::string_view key) const
{
	std::string parsed;
	if(!get(parsed, section, key))
		return false;
	value = std::move(parsed);
	return true;
}

bool TomlReader::get(
	bool &value,
	std::string_view section,
	std::string_view key) const
{
	auto parsed = get_bool(section, key);
	if(!parsed)
		return false;
	value = *parsed;
	return true;
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

static size_t attached_comment_begin(std::string_view document, size_t line_begin) {
	size_t attached_begin = line_begin;
	while (attached_begin > 0) {
		size_t previous_line_end = attached_begin;
		if (document[previous_line_end - 1] == '\n') --previous_line_end;
		if (previous_line_end > 0 && document[previous_line_end - 1] == '\r') {
			--previous_line_end;
		}

		const auto previous_newline =
			previous_line_end == 0
				? std::string_view::npos
				: document.rfind('\n', previous_line_end - 1);
		const auto previous_line_begin =
			previous_newline == std::string_view::npos ? 0 : previous_newline + 1;
		const auto previous_line = trim(document.substr(
			previous_line_begin,
			previous_line_end - previous_line_begin));
		if (previous_line.empty() || previous_line.front() != '#') break;
		attached_begin = previous_line_begin;
	}
	return attached_begin;
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
	m_source_document = std::move(document);
	m_sections.clear();
	m_current_section.reset();
	return true;
}

void TomlWriter::section(std::string_view name) {
	for (size_t i = 0; i < m_sections.size(); ++i) {
		if (m_sections[i].name == name) {
			m_current_section = i;
			return;
		}
	}

	m_sections.push_back({std::string(name), {}});
	m_current_section = m_sections.size() - 1;
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
	if (!m_current_section) return;

	auto &entries = m_sections[*m_current_section].entries;
	for (auto &entry : entries) {
		if (entry.key == key) {
			entry.value = std::move(value);
			return;
		}
	}
	entries.push_back({std::string(key), std::move(value)});
}

static void append_section(std::string &document, std::string_view name) {
	const auto newline = newline_for(document);
	if (!document.empty() && document.back() != '\n') document.append(newline);
	if (!document.empty()) document.append(newline);
	document += '[';
	document.append(name);
	document += ']';
	document.append(newline);
}

static std::optional<size_t> find_section_begin(
	std::string_view document,
	std::string_view name)
{
	for (size_t begin = 0; begin < document.size();) {
		const auto newline = document.find('\n', begin);
		const auto end = newline == std::string_view::npos ? document.size() : newline;
		auto line = document.substr(begin, end - begin);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		if (auto existing = section_name(line); existing && *existing == name) {
			return attached_comment_begin(document, begin);
		}
		begin = newline == std::string_view::npos ? document.size() : newline + 1;
	}
	return std::nullopt;
}

static std::optional<size_t> find_key_begin(
	std::string_view document,
	std::string_view section_name_to_find,
	std::string_view key)
{
	bool in_section = false;
	for (size_t begin = 0; begin < document.size();) {
		const auto newline = document.find('\n', begin);
		const auto end = newline == std::string_view::npos ? document.size() : newline;
		auto line = document.substr(begin, end - begin);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

		if (auto section = section_name(line)) {
			if (in_section) return std::nullopt;
			in_section = *section == section_name_to_find;
		} else if (in_section) {
			const auto equals = find_unquoted(line, '=');
			if (equals != std::string_view::npos && trim(line.substr(0, equals)) == key) {
				return attached_comment_begin(document, begin);
			}
		}

		begin = newline == std::string_view::npos ? document.size() : newline + 1;
	}
	return std::nullopt;
}

static void insert_section(
	std::string &document,
	size_t position,
	std::string_view name)
{
	const auto newline = newline_for(document);
	std::string block;

	if (position > 0) {
		size_t previous_line_end = position;
		if (previous_line_end > 0 && document[previous_line_end - 1] == '\n') --previous_line_end;
		if (previous_line_end > 0 && document[previous_line_end - 1] == '\r') --previous_line_end;
		const auto previous_newline = document.rfind('\n', previous_line_end - 1);
		const auto previous_line_begin =
			previous_newline == std::string::npos ? 0 : previous_newline + 1;
		if (!trim(std::string_view(document).substr(
		        previous_line_begin,
		        previous_line_end - previous_line_begin)).empty()) {
			block.append(newline);
		}
	}

	block += '[';
	block.append(name);
	block += ']';
	block.append(newline);
	block.append(newline);
	document.insert(position, block);
}

static void reconcile_value(
	std::string &document,
	std::string_view section_name_to_find,
	std::string_view key,
	std::string_view value,
	std::optional<size_t> insert_before)
{
	std::string current_section;
	size_t insertion = document.size();
	bool found_section = false;

	for (size_t begin = 0; begin < document.size();) {
		const auto newline_pos = document.find('\n', begin);
		const auto physical_end = newline_pos == std::string::npos ? document.size() : newline_pos;
		const auto next_begin = newline_pos == std::string::npos ? document.size() : newline_pos + 1;
		size_t content_end = physical_end;
		if (content_end > begin && document[content_end - 1] == '\r') --content_end;
		auto line = std::string_view(document).substr(begin, content_end - begin);

		if (auto section = section_name(line)) {
			if (found_section && *section != section_name_to_find) {
				break;
			}
			current_section = *section;
			found_section = current_section == section_name_to_find;
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
				document.replace(begin + value_begin, value_end - value_begin, value);
				return;
			}
			if (!strip_comment(line).empty()) insertion = next_begin;
		}

		begin = next_begin;
	}

	if (insert_before) insertion = *insert_before;
	const auto newline = newline_for(document);
	std::string assignment;
	assignment.reserve(key.size() + value.size() + newline.size() + 3);
	assignment.append(key);
	assignment += " = ";
	assignment += value;
	assignment.append(newline);
	if (insertion == document.size() && !document.empty() && document.back() != '\n') {
		assignment.insert(0, newline);
	}
	document.insert(insertion, assignment);
}

std::string TomlWriter::render(std::string document) const {
	for (size_t i = 0; i < m_sections.size(); ++i) {
		const auto &section = m_sections[i];
		if (!find_section_begin(document, section.name)) {
			std::optional<size_t> next_section;
			for (size_t j = i + 1; j < m_sections.size(); ++j) {
				next_section = find_section_begin(document, m_sections[j].name);
				if (next_section) break;
			}
			if (next_section) {
				insert_section(document, *next_section, section.name);
			} else {
				append_section(document, section.name);
			}
		}
		for (size_t j = 0; j < section.entries.size(); ++j) {
			const auto &entry = section.entries[j];
			std::optional<size_t> next_key;
			if (!find_key_begin(document, section.name, entry.key)) {
				for (size_t k = j + 1; k < section.entries.size(); ++k) {
					next_key = find_key_begin(
						document,
						section.name,
						section.entries[k].key);
					if (next_key) break;
				}
			}
			reconcile_value(
				document,
				section.name,
				entry.key,
				entry.value,
				next_key);
		}
	}
	return document;
}

bool TomlWriter::save(const std::filesystem::path &path) const {
	std::string document;
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (input) {
		char buffer[4096];
		while (input) {
			input.read(buffer, sizeof(buffer));
			const auto count = input.gcount();
			if (count > 0) document.append(buffer, static_cast<size_t>(count));
		}
		if (input.bad()) return false;
	} else {
		std::error_code error;
		if (std::filesystem::exists(path, error) || error) return false;
	}

	document = render(std::move(document));
	std::ofstream f(path, std::ios::out | std::ios::binary);
	if (!f) return false;
	f.write(document.data(), static_cast<std::streamsize>(document.size()));
	if (!f.good()) return false;
	f.close();
	if (!f) return false;
	return true;
}

bool TomlWriter::save(std::ostream &output) const {
	const auto document = render(m_source_document);
	output.write(document.data(), static_cast<std::streamsize>(document.size()));
	return output.good();
}
