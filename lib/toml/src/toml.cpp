// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>

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

// ─── TomlReader ──────────────────────────────────────────────────────────────

namespace {

bool valid_utf8(std::string_view text) {
	for (size_t i = 0; i < text.size();) {
		const auto c = static_cast<unsigned char>(text[i]);
		if (c < 0x80) {
			++i;
			continue;
		}
		size_t count = 0;
		uint32_t codepoint = 0;
		if ((c & 0xe0) == 0xc0) {
			count = 2;
			codepoint = c & 0x1f;
			if (codepoint < 2) return false;
		} else if ((c & 0xf0) == 0xe0) {
			count = 3;
			codepoint = c & 0x0f;
		} else if ((c & 0xf8) == 0xf0) {
			count = 4;
			codepoint = c & 0x07;
			if (codepoint > 4) return false;
		} else {
			return false;
		}
		if (i + count > text.size()) return false;
		for (size_t j = 1; j < count; ++j) {
			const auto next = static_cast<unsigned char>(text[i + j]);
			if ((next & 0xc0) != 0x80) return false;
			codepoint = (codepoint << 6) | (next & 0x3f);
		}
		if ((count == 3 && codepoint < 0x800) ||
		    (count == 4 && codepoint < 0x10000) ||
		    codepoint > 0x10ffff ||
		    (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
			return false;
		}
		i += count;
	}
	return true;
}

bool is_bare_key_char(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

bool is_digit_for_base(char c, int base) {
	if (c >= '0' && c <= '9') return c - '0' < base;
	if (c >= 'a' && c <= 'f') return base == 16;
	if (c >= 'A' && c <= 'F') return base == 16;
	return false;
}

bool leap_year(int year) {
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool valid_date(int year, int month, int day) {
	static constexpr int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (year < 0 || month < 1 || month > 12 || day < 1) return false;
	int limit = days[month] + (month == 2 && leap_year(year) ? 1 : 0);
	return day <= limit;
}

class Parser {
public:
	Parser(std::string_view input, TomlDocument &document)
		: m_input(input), m_document(document), m_current(&document.root) {
		m_document.root = TomlValue{TomlValue::Type::Table};
		m_document.root.explicit_table = true;
	}

	bool parse() {
		if (!valid_utf8(m_input)) return false;
		if (m_input.starts_with("\xef\xbb\xbf")) {
			advance();
			advance();
			advance();
		}
		while (!eof()) {
			skip_spaces();
			if (eof()) break;
			if (peek() == '#') {
				if (!parse_comment(false)) return false;
			} else if (newline_here()) {
				if (!consume_newline()) return false;
			} else {
				if (peek() == '[') {
					if (!parse_header()) return false;
				} else {
					if (!parse_assignment()) return false;
				}
				skip_spaces();
				if (!eof() && peek() == '#') {
					if (!parse_comment(true)) return false;
				}
				if (!eof() && !consume_newline()) return false;
			}
		}
		return true;
	}

private:
	std::string_view m_input;
	TomlDocument &m_document;
	TomlValue *m_current;
	size_t m_pos = 0;
	size_t m_line = 1;
	size_t m_column = 1;

	bool eof() const { return m_pos >= m_input.size(); }
	char peek(size_t offset = 0) const {
		return m_pos + offset < m_input.size() ? m_input[m_pos + offset] : '\0';
	}
	void advance() {
		if (eof()) return;
		if (m_input[m_pos] == '\n') {
			++m_line;
			m_column = 1;
		} else {
			++m_column;
		}
		++m_pos;
	}
	bool newline_here() const { return peek() == '\n' || peek() == '\r'; }
	bool consume_newline() {
		if (peek() == '\r') {
			if (peek(1) != '\n') return false;
			advance();
			advance();
			return true;
		}
		if (peek() != '\n') return false;
		advance();
		return true;
	}
	void skip_spaces() {
		while (peek() == ' ' || peek() == '\t') advance();
	}
	bool parse_comment(bool trailing) {
		const auto offset = m_pos;
		const auto line = m_line;
		const auto column = m_column;
		if (peek() != '#') return false;
		advance();
		const auto begin = m_pos;
		while (!eof() && !newline_here()) {
			const auto c = static_cast<unsigned char>(peek());
			if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
			advance();
		}
		m_document.comments.push_back({
			std::string(m_input.substr(begin, m_pos - begin)),
			offset,
			line,
			column,
			trailing
		});
		return true;
	}

	bool parse_header() {
		advance();
		const bool array = peek() == '[';
		if (array) advance();
		std::vector<std::string> path;
		if (!parse_key_path(path, ']')) return false;
		if (peek() != ']') return false;
		advance();
		if (array) {
			if (peek() != ']') return false;
			advance();
		}
		return open_table(path, array);
	}

	bool parse_assignment() {
		std::vector<std::string> path;
		if (!parse_key_path(path, '=')) return false;
		if (peek() != '=') return false;
		advance();
		skip_spaces();
		TomlValue value;
		if (!parse_value(value)) return false;
		return insert_value(*m_current, path, std::move(value), false);
	}

	bool parse_key_path(std::vector<std::string> &path, char terminator) {
		skip_spaces();
		while (true) {
			std::string part;
			if (!parse_key_part(part)) return false;
			path.push_back(std::move(part));
			skip_spaces();
			if (peek() == terminator) return true;
			if (peek() != '.') return false;
			advance();
			skip_spaces();
		}
	}

	bool parse_key_part(std::string &part) {
		if (peek() == '"') return parse_basic_string(part, false);
		if (peek() == '\'') return parse_literal_string(part, false);
		const auto begin = m_pos;
		while (is_bare_key_char(peek())) advance();
		if (m_pos == begin) return false;
		part.assign(m_input.substr(begin, m_pos - begin));
		return true;
	}

	bool parse_value(TomlValue &value) {
		if (eof()) return false;
		if (peek() == '"') {
			value = TomlValue{TomlValue::Type::String};
			return parse_basic_string(value.str, true);
		}
		if (peek() == '\'') {
			value = TomlValue{TomlValue::Type::String};
			return parse_literal_string(value.str, true);
		}
		if (peek() == '[') return parse_array(value);
		if (peek() == '{') return parse_inline_table(value);
		return parse_token(value);
	}

	bool parse_basic_string(std::string &output, bool multiline_allowed) {
		if (peek() != '"') return false;
		const bool multiline = peek(1) == '"' && peek(2) == '"';
		if (multiline && !multiline_allowed) return false;
		advance();
		if (multiline) {
			advance();
			advance();
			if (newline_here() && !consume_newline()) return false;
		}

		while (!eof()) {
			if (peek() == '"') {
				size_t quotes = 0;
				while (peek(quotes) == '"') ++quotes;
				if (!multiline) {
					advance();
					return true;
				}
				if (quotes >= 3 && quotes <= 5) {
					for (size_t i = 0; i < quotes - 3; ++i) output.push_back('"');
					for (size_t i = 0; i < quotes; ++i) advance();
					return true;
				}
				if (quotes > 5) return false;
				for (size_t i = 0; i < quotes; ++i) {
					output.push_back('"');
					advance();
				}
				continue;
			}
			if (newline_here()) {
				if (!multiline || !consume_newline()) return false;
				output.push_back('\n');
				continue;
			}
			if (peek() == '\\') {
				advance();
				if (multiline && (newline_here() || peek() == ' ' || peek() == '\t')) {
					while (peek() == ' ' || peek() == '\t') advance();
					if (!newline_here() || !consume_newline()) return false;
					while (!eof()) {
						if (peek() == ' ' || peek() == '\t') {
							advance();
						} else if (newline_here()) {
							if (!consume_newline()) return false;
						} else {
							break;
						}
					}
					continue;
				}
				if (!parse_escape(output)) return false;
				continue;
			}
			const auto c = static_cast<unsigned char>(peek());
			if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
			output.push_back(peek());
			advance();
		}
		return false;
	}

	bool parse_escape(std::string &output) {
		if (eof()) return false;
		switch (peek()) {
			case 'b': output.push_back('\b'); advance(); return true;
			case 't': output.push_back('\t'); advance(); return true;
			case 'n': output.push_back('\n'); advance(); return true;
			case 'f': output.push_back('\f'); advance(); return true;
			case 'r': output.push_back('\r'); advance(); return true;
			case '"': output.push_back('"'); advance(); return true;
			case '\\': output.push_back('\\'); advance(); return true;
			case 'u': advance(); return parse_unicode_escape(output, 4);
			case 'U': advance(); return parse_unicode_escape(output, 8);
			default: return false;
		}
	}

	bool parse_unicode_escape(std::string &output, size_t digits) {
		uint32_t codepoint = 0;
		for (size_t i = 0; i < digits; ++i) {
			const char c = peek();
			uint32_t digit;
			if (c >= '0' && c <= '9') digit = c - '0';
			else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
			else return false;
			codepoint = (codepoint << 4) | digit;
			advance();
		}
		if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
		if (codepoint <= 0x7f) {
			output.push_back(static_cast<char>(codepoint));
		} else if (codepoint <= 0x7ff) {
			output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else if (codepoint <= 0xffff) {
			output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else {
			output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
		return true;
	}

	bool parse_literal_string(std::string &output, bool multiline_allowed) {
		if (peek() != '\'') return false;
		const bool multiline = peek(1) == '\'' && peek(2) == '\'';
		if (multiline && !multiline_allowed) return false;
		advance();
		if (multiline) {
			advance();
			advance();
			if (newline_here() && !consume_newline()) return false;
		}
		while (!eof()) {
			if (peek() == '\'') {
				size_t quotes = 0;
				while (peek(quotes) == '\'') ++quotes;
				if (!multiline) {
					advance();
					return true;
				}
				if (quotes >= 3 && quotes <= 5) {
					for (size_t i = 0; i < quotes - 3; ++i) output.push_back('\'');
					for (size_t i = 0; i < quotes; ++i) advance();
					return true;
				}
				if (quotes > 5) return false;
				for (size_t i = 0; i < quotes; ++i) {
					output.push_back('\'');
					advance();
				}
				continue;
			}
			if (newline_here()) {
				if (!multiline || !consume_newline()) return false;
				output.push_back('\n');
				continue;
			}
			const auto c = static_cast<unsigned char>(peek());
			if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
			output.push_back(peek());
			advance();
		}
		return false;
	}

	bool skip_array_space(bool trailing) {
		while (true) {
			skip_spaces();
			if (peek() == '#') {
				if (!parse_comment(trailing)) return false;
				trailing = false;
			} else if (newline_here()) {
				if (!consume_newline()) return false;
				trailing = false;
			} else {
				return true;
			}
		}
	}

	bool parse_array(TomlValue &value) {
		value = TomlValue{TomlValue::Type::Array};
		advance();
		if (!skip_array_space(false)) return false;
		if (peek() == ']') {
			advance();
			return true;
		}
		while (true) {
			TomlValue element;
			if (!parse_value(element)) return false;
			value.array.push_back(std::move(element));
			if (!skip_array_space(true)) return false;
			if (peek() == ']') {
				advance();
				return true;
			}
			if (peek() != ',') return false;
			advance();
			if (!skip_array_space(false)) return false;
			if (peek() == ']') {
				advance();
				return true;
			}
		}
	}

	bool parse_inline_table(TomlValue &value) {
		value = TomlValue{TomlValue::Type::Table};
		value.inline_table = true;
		advance();
		skip_spaces();
		if (peek() == '}') {
			advance();
			return true;
		}
		while (true) {
			std::vector<std::string> path;
			if (!parse_key_path(path, '=')) return false;
			if (peek() != '=') return false;
			advance();
			skip_spaces();
			TomlValue element;
			if (!parse_value(element)) return false;
			if (!insert_value(value, path, std::move(element), true)) return false;
			skip_spaces();
			if (peek() == '}') {
				advance();
				mark_inline(value);
				return true;
			}
			if (peek() != ',') return false;
			advance();
			skip_spaces();
			if (peek() == '}') return false;
		}
	}

	void mark_inline(TomlValue &value) {
		if (value.type != TomlValue::Type::Table) return;
		value.inline_table = true;
		for (auto &[key, child] : value.table) {
			(void)key;
			mark_inline(child);
		}
	}

	bool parse_token(TomlValue &value) {
		const auto begin = m_pos;
		while (!eof() && peek() != ',' && peek() != ']' && peek() != '}' &&
		       peek() != '#' && !newline_here()) {
			advance();
		}
		auto token = m_input.substr(begin, m_pos - begin);
		while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
			token.remove_suffix(1);
		}
		if (token.empty() || token.front() == ' ' || token.front() == '\t') return false;
		if (token == "true" || token == "false") {
			value = TomlValue{TomlValue::Type::Bool};
			value.b = token == "true";
			return true;
		}
		if (parse_datetime(token, value)) return true;
		return parse_number(token, value);
	}

	static bool parse_fixed_digits(std::string_view text, size_t pos, size_t count, int &value) {
		if (pos + count > text.size()) return false;
		value = 0;
		for (size_t i = 0; i < count; ++i) {
			if (!std::isdigit(static_cast<unsigned char>(text[pos + i]))) return false;
			value = value * 10 + text[pos + i] - '0';
		}
		return true;
	}

	bool parse_datetime(std::string_view token, TomlValue &value) {
		int year, month, day;
		const bool starts_date =
			token.size() >= 10 &&
			parse_fixed_digits(token, 0, 4, year) &&
			token[4] == '-' &&
			parse_fixed_digits(token, 5, 2, month) &&
			token[7] == '-' &&
			parse_fixed_digits(token, 8, 2, day);
		if (starts_date) {
			if (!valid_date(year, month, day)) return false;
			if (token.size() == 10) {
				value = TomlValue{TomlValue::Type::DateLocal};
				value.str = token;
				return true;
			}
			if (token[10] != 'T' && token[10] != 't' && token[10] != ' ') return false;
			size_t end = 0;
			bool offset = false;
			if (!valid_time(token, 11, end, offset) || end != token.size()) return false;
			value = TomlValue{
				offset ? TomlValue::Type::DateTime : TomlValue::Type::DateTimeLocal};
			value.str = token;
			return true;
		}

		size_t end = 0;
		bool offset = false;
		if (valid_time(token, 0, end, offset) && end == token.size() && !offset) {
			value = TomlValue{TomlValue::Type::TimeLocal};
			value.str = token;
			return true;
		}
		return false;
	}

	static bool valid_time(std::string_view text, size_t pos, size_t &end, bool &offset) {
		int hour, minute, second;
		if (!parse_fixed_digits(text, pos, 2, hour) ||
		    pos + 8 > text.size() || text[pos + 2] != ':' ||
		    !parse_fixed_digits(text, pos + 3, 2, minute) ||
		    text[pos + 5] != ':' ||
		    !parse_fixed_digits(text, pos + 6, 2, second) ||
		    hour > 23 || minute > 59 || second > 59) {
			return false;
		}
		pos += 8;
		if (pos < text.size() && text[pos] == '.') {
			++pos;
			const auto fraction = pos;
			while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
			if (pos == fraction) return false;
		}
		offset = false;
		if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
			offset = true;
			++pos;
		} else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
			offset = true;
			int offset_hour, offset_minute;
			++pos;
			if (!parse_fixed_digits(text, pos, 2, offset_hour) ||
			    pos + 5 > text.size() || text[pos + 2] != ':' ||
			    !parse_fixed_digits(text, pos + 3, 2, offset_minute) ||
			    offset_hour > 23 || offset_minute > 59) {
				return false;
			}
			pos += 5;
		}
		end = pos;
		return true;
	}

	bool parse_number(std::string_view token, TomlValue &value) {
		size_t pos = 0;
		bool negative = false;
		if (token[pos] == '+' || token[pos] == '-') {
			negative = token[pos] == '-';
			if (++pos == token.size()) return false;
		}
		const auto body = token.substr(pos);
		if (body == "inf" || body == "nan") {
			value = TomlValue{TomlValue::Type::Float};
			value.f = body == "inf"
				? std::numeric_limits<double>::infinity()
				: std::numeric_limits<double>::quiet_NaN();
			if (negative) value.f = -value.f;
			return true;
		}

		if (pos == 0 && body.size() > 2 && body[0] == '0' &&
		    (body[1] == 'x' || body[1] == 'o' || body[1] == 'b')) {
			const int base = body[1] == 'x' ? 16 : body[1] == 'o' ? 8 : 2;
			std::string digits;
			if (!clean_digits(body.substr(2), base, digits)) return false;
			uint64_t parsed = 0;
			const auto result = std::from_chars(
				digits.data(), digits.data() + digits.size(), parsed, base);
			if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
			    parsed > static_cast<uint64_t>(INT64_MAX)) {
				return false;
			}
			value = TomlValue{TomlValue::Type::Integer};
			value.i = static_cast<int64_t>(parsed);
			return true;
		}

		const bool floating =
			body.find('.') != std::string_view::npos ||
			body.find('e') != std::string_view::npos ||
			body.find('E') != std::string_view::npos;
		std::string cleaned;
		if (!clean_decimal(token, floating, cleaned)) return false;
		if (floating) {
			double parsed = 0;
			auto number = std::string_view(cleaned);
			if (!number.empty() && number.front() == '+') number.remove_prefix(1);
			const auto result = std::from_chars(
				number.data(), number.data() + number.size(), parsed);
			if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) {
				return false;
			}
			value = TomlValue{TomlValue::Type::Float};
			value.f = parsed;
			return true;
		}

		int64_t parsed = 0;
		auto number = std::string_view(cleaned);
		bool plus = !number.empty() && number.front() == '+';
		if (plus) number.remove_prefix(1);
		const auto result = std::from_chars(
			number.data(), number.data() + number.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) return false;
		value = TomlValue{TomlValue::Type::Integer};
		value.i = parsed;
		return true;
	}

	static bool clean_digits(std::string_view input, int base, std::string &output) {
		if (input.empty()) return false;
		for (size_t i = 0; i < input.size(); ++i) {
			if (input[i] == '_') {
				if (i == 0 || i + 1 == input.size() ||
				    !is_digit_for_base(input[i - 1], base) ||
				    !is_digit_for_base(input[i + 1], base)) {
					return false;
				}
			} else if (!is_digit_for_base(input[i], base)) {
				return false;
			} else {
				output.push_back(input[i]);
			}
		}
		return !output.empty();
	}

	static bool clean_decimal(std::string_view input, bool floating, std::string &output) {
		size_t pos = 0;
		if (input[pos] == '+' || input[pos] == '-') {
			output.push_back(input[pos++]);
			if (pos == input.size()) return false;
		}
		const auto integer_start = pos;
		if (!consume_decimal_digits(input, pos, output)) return false;
		std::string integer_digits;
		for (size_t i = integer_start; i < pos; ++i) {
			if (input[i] != '_') integer_digits.push_back(input[i]);
		}
		if (integer_digits.size() > 1 && integer_digits.front() == '0') return false;
		if (!floating) return pos == input.size();
		if (pos < input.size() && input[pos] == '.') {
			output.push_back(input[pos++]);
			if (!consume_decimal_digits(input, pos, output)) return false;
		}
		if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
			output.push_back(input[pos++]);
			if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) {
				output.push_back(input[pos++]);
			}
			if (!consume_decimal_digits(input, pos, output)) return false;
		}
		return pos == input.size();
	}

	static bool consume_decimal_digits(
		std::string_view input,
		size_t &pos,
		std::string &output)
	{
		const auto begin = pos;
		bool previous_digit = false;
		while (pos < input.size()) {
			const char c = input[pos];
			if (std::isdigit(static_cast<unsigned char>(c))) {
				output.push_back(c);
				previous_digit = true;
				++pos;
			} else if (c == '_') {
				if (!previous_digit || pos + 1 >= input.size() ||
				    !std::isdigit(static_cast<unsigned char>(input[pos + 1]))) {
					return false;
				}
				previous_digit = false;
				++pos;
			} else {
				break;
			}
		}
		return pos > begin && previous_digit;
	}

	bool open_table(const std::vector<std::string> &path, bool array) {
		if (path.empty()) return false;
		TomlValue *table = &m_document.root;
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			table = descend_header(*table, path[i]);
			if (!table) return false;
		}
		const auto &name = path.back();
		auto *child = table->find(name);
		if (array) {
			if (!child) {
				TomlValue list{TomlValue::Type::Array};
				list.array_of_tables = true;
				child = &table->insert(name, std::move(list));
			}
			if (child->type != TomlValue::Type::Array ||
			    !child->array_of_tables) {
				return false;
			}
			TomlValue element{TomlValue::Type::Table};
			element.explicit_table = true;
			child->array.push_back(std::move(element));
			m_current = &child->array.back();
			return true;
		}

		if (!child) {
			TomlValue new_table{TomlValue::Type::Table};
			new_table.explicit_table = true;
			child = &table->insert(name, std::move(new_table));
		} else {
			if (child->type != TomlValue::Type::Table ||
			    child->explicit_table || child->dotted_table || child->inline_table) {
				return false;
			}
			child->explicit_table = true;
		}
		m_current = child;
		return true;
	}

	TomlValue *descend_header(TomlValue &table, const std::string &name) {
		auto *child = table.find(name);
		if (!child) {
			child = &table.insert(name, TomlValue{TomlValue::Type::Table});
		}
		if (child->type == TomlValue::Type::Table) {
			if (child->inline_table) return nullptr;
			return child;
		}
		if (child->type == TomlValue::Type::Array && child->array_of_tables &&
		    !child->array.empty() &&
		    child->array.back().type == TomlValue::Type::Table) {
			return &child->array.back();
		}
		return nullptr;
	}

	bool insert_value(
		TomlValue &table,
		const std::vector<std::string> &path,
		TomlValue value,
		bool in_inline)
	{
		if (path.empty() || table.type != TomlValue::Type::Table) return false;
		TomlValue *cursor = &table;
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			auto *child = cursor->find(path[i]);
			if (!child) {
				TomlValue new_table{TomlValue::Type::Table};
				new_table.dotted_table = true;
				new_table.inline_table = in_inline;
				auto &inserted = cursor->insert(path[i], std::move(new_table));
				child = &inserted;
			}
			if (child->type != TomlValue::Type::Table ||
			    child->explicit_table ||
			    (child->inline_table && !child->dotted_table)) {
				return false;
			}
			cursor = child;
		}
		if (cursor->find(path.back())) return false;
		cursor->insert(path.back(), std::move(value));
		return true;
	}
};

} // namespace

bool TomlReader::load(const std::filesystem::path &path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input) return false;
	return load(input);
}

bool TomlReader::load(std::istream &input) {
	std::string text;
	char buffer[4096];
	while (input) {
		input.read(buffer, sizeof(buffer));
		const auto count = input.gcount();
		if (count > 0) text.append(buffer, static_cast<size_t>(count));
	}
	if (input.bad()) {
		m_document = {};
		return false;
	}
	TomlDocument parsed;
	Parser parser(text, parsed);
	if (!parser.parse()) {
		m_document = {};
		return false;
	}
	m_document = std::move(parsed);
	return true;
}

const TomlValue *TomlReader::find(std::string_view section, std::string_view key) const {
	const TomlValue *table = &m_document.root;
	while (!section.empty()) {
		const auto dot = section.find('.');
		const auto part = section.substr(0, dot);
		const auto *child = table->find(part);
		if (!child || child->type != TomlValue::Type::Table) {
			return nullptr;
		}
		table = child;
		if (dot == std::string_view::npos) break;
		section.remove_prefix(dot + 1);
	}
	return table->find(key);
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
