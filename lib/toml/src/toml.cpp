// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

std::string &TomlValue::text() {
	if (auto *value = std::get_if<std::string>(&data)) return *value;
	if (auto *value = std::get_if<TomlOffsetDateTime>(&data)) return value->value;
	if (auto *value = std::get_if<TomlLocalDateTime>(&data)) return value->value;
	if (auto *value = std::get_if<TomlLocalDate>(&data)) return value->value;
	if (auto *value = std::get_if<TomlLocalTime>(&data)) return value->value;
	throw std::bad_variant_access{};
}

const std::string &TomlValue::text() const {
	if (const auto *value = std::get_if<std::string>(&data)) return *value;
	if (const auto *value = std::get_if<TomlOffsetDateTime>(&data)) return value->value;
	if (const auto *value = std::get_if<TomlLocalDateTime>(&data)) return value->value;
	if (const auto *value = std::get_if<TomlLocalDate>(&data)) return value->value;
	if (const auto *value = std::get_if<TomlLocalTime>(&data)) return value->value;
	throw std::bad_variant_access{};
}

// ─── TOML parsing ────────────────────────────────────────────────────────────

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
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') ||
	       c == '_' ||
	       c == '-';
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
		m_document.root = TomlValue{TomlTable{}};
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
				TomlComment comment;
				if (!read_comment(comment)) return false;
				m_pending_comments.push_back(std::move(comment));
			} else if (newline_here()) {
				if (!consume_newline()) return false;
			} else {
				m_statement_value = nullptr;
				if (peek() == '[') {
					if (!parse_header()) return false;
				} else {
					if (!parse_assignment()) return false;
				}
				if (!m_statement_value) return false;
				m_statement_value->leading_comments.insert(
					m_statement_value->leading_comments.end(),
					std::make_move_iterator(m_pending_comments.begin()),
					std::make_move_iterator(m_pending_comments.end()));
				m_pending_comments.clear();
				skip_spaces();
				if (!eof() && peek() == '#') {
					TomlComment comment;
					if (!read_comment(comment)) return false;
					m_statement_value->trailing_comment = std::move(comment);
				}
				if (!eof() && !consume_newline()) return false;
			}
		}
		m_document.trailing_comments.insert(
			m_document.trailing_comments.end(),
			std::make_move_iterator(m_pending_comments.begin()),
			std::make_move_iterator(m_pending_comments.end()));
		m_pending_comments.clear();
		return true;
	}

	size_t line() const noexcept { return m_line; }
	size_t column() const noexcept { return m_column; }

private:
	std::string_view m_input;
	TomlDocument &m_document;
	TomlValue *m_current;
	TomlValue *m_statement_value = nullptr;
	std::vector<TomlComment> m_pending_comments;
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
	bool read_comment(TomlComment &comment) {
		if (peek() != '#') return false;
		advance();
		const auto begin = m_pos;
		while (!eof() && !newline_here()) {
			const auto c = static_cast<unsigned char>(peek());
			if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
			advance();
		}
		comment.text = std::string(m_input.substr(begin, m_pos - begin));
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
		if (!open_table(path, array)) return false;
		m_statement_value = m_current;
		return true;
	}

	bool parse_assignment() {
		skip_spaces();
		std::string key;
		if (!parse_key_part(key)) return false;
		skip_spaces();

		std::vector<std::string> path;
		const bool direct = peek() == '=';
		if (!direct) {
			path.push_back(std::move(key));
			if (!parse_key_path_tail(path, '=')) return false;
		}
		if (peek() != '=') return false;
		advance();
		skip_spaces();
		TomlValue value;
		if (!parse_value(value)) return false;
		if (direct) {
			auto [entry, inserted] =
				m_current->table().try_emplace(std::move(key), std::move(value));
			if (!inserted) return false;
			m_statement_value = &entry.value();
			return true;
		}
		return insert_value(
			*m_current,
			path,
			std::move(value),
			false,
			&m_statement_value);
	}

	bool parse_key_path(std::vector<std::string> &path, char terminator) {
		skip_spaces();
		std::string part;
		if (!parse_key_part(part)) return false;
		path.push_back(std::move(part));
		skip_spaces();
		return parse_key_path_tail(path, terminator);
	}

	bool parse_key_path_tail(std::vector<std::string> &path, char terminator) {
		if (peek() == terminator) return true;
		while (peek() == '.') {
			advance();
			skip_spaces();
			std::string part;
			if (!parse_key_part(part)) return false;
			path.push_back(std::move(part));
			skip_spaces();
			if (peek() == terminator) return true;
		}
		return false;
	}

	bool parse_key_part(std::string &part) {
		if (peek() == '"') return parse_basic_string(part, false);
		if (peek() == '\'') return parse_literal_string(part, false);
		const auto begin = m_pos;
		while (is_bare_key_char(peek())) {
			++m_pos;
			++m_column;
		}
		if (m_pos == begin) return false;
		part.assign(m_input.substr(begin, m_pos - begin));
		return true;
	}

	bool parse_value(TomlValue &value) {
		if (eof()) return false;
		if (peek() == '"') {
			value = TomlValue{std::string{}};
			return parse_basic_string(value.text(), true);
		}
		if (peek() == '\'') {
			value = TomlValue{std::string{}};
			return parse_literal_string(value.text(), true);
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
			++m_pos;
			++m_column;
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

	bool skip_array_space(
		TomlValue &array,
		TomlValue *last,
		std::vector<TomlComment> &pending,
		bool trailing)
	{
		while (true) {
			skip_spaces();
			if (peek() == '#') {
				TomlComment comment;
				if (!read_comment(comment)) return false;
				if (trailing && last) {
					last->trailing_comment = std::move(comment);
				} else {
					pending.push_back(std::move(comment));
				}
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
		value = TomlValue{TomlArray{}};
		advance();
		std::vector<TomlComment> pending;
		if (!skip_array_space(value, nullptr, pending, false)) return false;
		if (peek() == ']') {
			value.dangling_comments = std::move(pending);
			advance();
			return true;
		}
		while (true) {
			TomlValue element;
			if (!parse_value(element)) return false;
			element.leading_comments = std::move(pending);
			pending.clear();
			value.array().push_back(std::move(element));
			if (!skip_array_space(value, &value.array().back(), pending, true)) return false;
			if (peek() == ']') {
				value.dangling_comments = std::move(pending);
				advance();
				return true;
			}
			if (peek() != ',') return false;
			advance();
			if (!skip_array_space(value, nullptr, pending, false)) return false;
			if (peek() == ']') {
				value.dangling_comments = std::move(pending);
				advance();
				return true;
			}
		}
	}

	bool parse_inline_table(TomlValue &value) {
		value = TomlValue{TomlTable{}};
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
			if (!insert_value(value, path, std::move(element), true, nullptr)) return false;
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
		if (!value.is<TomlTable>()) return;
		value.inline_table = true;
		for (auto entry = value.table().begin(); entry != value.table().end(); ++entry) {
			mark_inline(entry.value());
		}
	}

	bool parse_token(TomlValue &value) {
		const auto begin = m_pos;
		while (!eof() && peek() != ',' && peek() != ']' && peek() != '}' &&
		       peek() != '#' && !newline_here()) {
			++m_pos;
			++m_column;
		}
		auto token = m_input.substr(begin, m_pos - begin);
		while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
			token.remove_suffix(1);
		}
		if (token.empty() || token.front() == ' ' || token.front() == '\t') return false;
		if (token == "true" || token == "false") {
			value = TomlValue{false};
			value.boolean() = token == "true";
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
				value = TomlValue{TomlLocalDate{}};
				value.text() = token;
				return true;
			}
			if (token[10] != 'T' && token[10] != 't' && token[10] != ' ') return false;
			size_t end = 0;
			bool offset = false;
			if (!valid_time(token, 11, end, offset) || end != token.size()) return false;
			value = offset
				? TomlValue{TomlOffsetDateTime{}}
				: TomlValue{TomlLocalDateTime{}};
			value.text() = token;
			return true;
		}

		size_t end = 0;
		bool offset = false;
		if (valid_time(token, 0, end, offset) && end == token.size() && !offset) {
			value = TomlValue{TomlLocalTime{}};
			value.text() = token;
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
			value = TomlValue{double{}};
			value.floating() = body == "inf"
				? std::numeric_limits<double>::infinity()
				: std::numeric_limits<double>::quiet_NaN();
			if (negative) value.floating() = -value.floating();
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
			value = TomlValue{int64_t{}};
			value.integer() = static_cast<int64_t>(parsed);
			value.format_width = digits.size();
			if (base == 16) {
				const bool uppercase = std::any_of(
					body.begin() + 2,
					body.end(),
					[](unsigned char c) { return c >= 'A' && c <= 'F'; });
				value.format = uppercase
					? TomlValueFormat::IntegerHexUpper
					: TomlValueFormat::IntegerHexLower;
			} else {
				value.format = base == 8
					? TomlValueFormat::IntegerOctal
					: TomlValueFormat::IntegerBinary;
			}
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
			value = TomlValue{double{}};
			value.floating() = parsed;
			if (body.find('E') != std::string_view::npos) {
				value.format = TomlValueFormat::FloatScientificUpper;
			} else if (body.find('e') != std::string_view::npos) {
				value.format = TomlValueFormat::FloatScientificLower;
			}
			return true;
		}

		int64_t parsed = 0;
		auto number = std::string_view(cleaned);
		bool plus = !number.empty() && number.front() == '+';
		if (plus) number.remove_prefix(1);
		const auto result = std::from_chars(
			number.data(), number.data() + number.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) return false;
		value = TomlValue{int64_t{}};
		value.integer() = parsed;
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
				TomlValue list{TomlArray{}};
				list.array_of_tables = true;
				child = &table->insert(name, std::move(list));
			}
			if (!child->is<TomlArray>() ||
			    !child->array_of_tables) {
				return false;
			}
			TomlValue element{TomlTable{}};
			element.explicit_table = true;
			child->array().push_back(std::move(element));
			m_current = &child->array().back();
			return true;
		}

		if (!child) {
			TomlValue new_table{TomlTable{}};
			new_table.explicit_table = true;
			child = &table->insert(name, std::move(new_table));
		} else {
			if (!child->is<TomlTable>() ||
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
			child = &table.insert(name, TomlValue{TomlTable{}});
		}
		if (child->is<TomlTable>()) {
			if (child->inline_table) return nullptr;
			return child;
		}
		if (child->is<TomlArray>() && child->array_of_tables &&
		    !child->array().empty() &&
		    child->array().back().is<TomlTable>()) {
			return &child->array().back();
		}
		return nullptr;
	}

	bool insert_value(
		TomlValue &table,
		const std::vector<std::string> &path,
		TomlValue value,
		bool in_inline,
		TomlValue **inserted)
	{
		if (path.empty() || !table.is<TomlTable>()) return false;
		TomlValue *cursor = &table;
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			auto *child = cursor->find(path[i]);
			if (!child) {
				TomlValue new_table{TomlTable{}};
				new_table.dotted_table = true;
				new_table.inline_table = in_inline;
				auto &inserted = cursor->insert(path[i], std::move(new_table));
				child = &inserted;
			}
			if (!child->is<TomlTable>() ||
			    child->explicit_table ||
			    (child->inline_table && !child->dotted_table)) {
				return false;
			}
			cursor = child;
		}
		auto [entry, was_inserted] =
			cursor->table().try_emplace(path.back(), std::move(value));
		if (!was_inserted) return false;
		if (inserted) *inserted = &entry.value();
		return true;
	}
};

} // namespace

toml::ParseResult toml::from_file(const std::filesystem::path &path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input) {
		return {
			std::nullopt,
			std::format("Could not open TOML file '{}'.", path.string())
		};
	}
	return from_stream(input);
}

toml::ParseResult toml::from_stream(std::istream &input) {
	std::string text;
	char buffer[4096];
	while (input) {
		input.read(buffer, sizeof(buffer));
		const auto count = input.gcount();
		if (count > 0) text.append(buffer, static_cast<size_t>(count));
	}
	if (input.bad()) {
		return {std::nullopt, "Could not read TOML input."};
	}
	return from_string(text);
}

toml::ParseResult toml::from_string(std::string_view input) {
	TomlDocument parsed;
	Parser parser(input, parsed);
	if (!parser.parse()) {
		return {
			std::nullopt,
			std::format(
				"TOML parse error at line {}, column {}.",
				parser.line(),
				parser.column())
		};
	}
	return {std::move(parsed), {}};
}

// ─── TomlReader ──────────────────────────────────────────────────────────────

const TomlValue *TomlReader::find(std::string_view section, std::string_view key) const {
	const TomlValue *table = &m_document.root;
	while (!section.empty()) {
		const auto dot = section.find('.');
		const auto part = section.substr(0, dot);
		const auto *child = table->find(part);
		if (!child || !child->is<TomlTable>()) {
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
	if (!v || !v->is<std::string>()) {
		return std::nullopt;
	}
	return v->text();
}

std::optional<int64_t> TomlReader::get_integer(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || !v->is<int64_t>()) {
		return std::nullopt;
	}
	return v->integer();
}

std::optional<double> TomlReader::get_float(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v) {
		return std::nullopt;
	}
	if (v->is<double>()) {
		return v->floating();
	}
	if (v->is<int64_t>()) {
		return static_cast<double>(v->integer());
	}
	return std::nullopt;
}

std::optional<bool> TomlReader::get_bool(std::string_view section, std::string_view key) const {
	auto *v = find(section, key);
	if (!v || !v->is<bool>()) {
		return std::nullopt;
	}
	return v->boolean();
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

TomlDocument &TomlWriter::document() noexcept {
	if (auto *owned = std::get_if<TomlDocument>(&m_document)) return *owned;
	return std::get<std::reference_wrapper<TomlDocument>>(m_document).get();
}

const TomlDocument &TomlWriter::document() const noexcept {
	if (const auto *owned = std::get_if<TomlDocument>(&m_document)) return *owned;
	return std::get<std::reference_wrapper<TomlDocument>>(m_document).get();
}

static bool contains_name(
	const std::vector<std::string> &names,
	std::string_view name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}

static void move_entries_before(
	TomlTable &entries,
	const std::vector<std::string> &names,
	std::string_view anchor)
{
	if (names.empty()) return;

	TomlTable reordered;
	reordered.reserve(entries.size());
	bool inserted = false;
	for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
		const auto &key = entry.key();
		if (!inserted && key == anchor) {
			for (const auto &name : names) {
				auto pending = entries.find(name);
				if (pending != entries.end()) {
					reordered.try_emplace(pending.key(), std::move(pending.value()));
				}
			}
			inserted = true;
		}
		if (!contains_name(names, key)) {
			reordered.try_emplace(key, std::move(entry.value()));
		}
	}
	if (!inserted) {
		for (const auto &name : names) {
			auto pending = entries.find(name);
			if (pending != entries.end()) {
				reordered.try_emplace(pending.key(), std::move(pending.value()));
			}
		}
	}
	entries = std::move(reordered);
}

void TomlWriter::section(std::string_view name) {
	auto &toml_document = document();
	auto *value = toml_document.root.find(name);
	if (!value) {
		TomlValue table{TomlTable{}};
		table.explicit_table = true;
		toml_document.root.insert(std::string(name), std::move(table));
		if (!contains_name(m_seen_sections, name)) {
			m_seen_sections.emplace_back(name);
			m_pending_sections.emplace_back(name);
		}
	} else if (!value->is<TomlTable>()) {
		m_current_section.reset();
		return;
	} else if (!contains_name(m_seen_sections, name)) {
		move_entries_before(toml_document.root.table(), m_pending_sections, name);
		m_pending_sections.clear();
		m_seen_sections.emplace_back(name);
	}
	m_current_section = std::string(name);
}

static void serialize_string(std::ostream &output, std::string_view value) {
	output.put('"');
	for (const unsigned char c : value) {
		switch (c) {
			case '\b': output.write("\\b", 2);  break;
			case '\f': output.write("\\f", 2);  break;
			case '"':  output.write("\\\"", 2); break;
			case '\\': output.write("\\\\", 2); break;
			case '\n': output.write("\\n", 2);  break;
			case '\r': output.write("\\r", 2);  break;
			case '\t': output.write("\\t", 2);  break;
			default:
				if (c < 0x20 || c == 0x7f) {
					output << std::format("\\u{:04X}", c);
				} else {
					output.put(static_cast<char>(c));
				}
				break;
		}
	}
	output.put('"');
}

void TomlWriter::write(std::string_view key, std::string_view value) {
	TomlValue toml_value{std::string{}};
	toml_value.text() = value;
	write_value(key, std::move(toml_value));
}

void TomlWriter::write(std::string_view key, int64_t value) {
	TomlValue toml_value{int64_t{}};
	toml_value.integer() = value;
	write_value(key, std::move(toml_value));
}

void TomlWriter::write_hex(std::string_view key, int64_t value) {
	TomlValue toml_value{int64_t{}};
	toml_value.integer() = value;
	toml_value.format = TomlValueFormat::IntegerHexLower;
	toml_value.format_width = 4;
	write_value(key, std::move(toml_value));
}

void TomlWriter::write(std::string_view key, double value) {
	TomlValue toml_value{double{}};
	toml_value.floating() = value;
	toml_value.format = TomlValueFormat::FloatCompact;
	write_value(key, std::move(toml_value));
}

void TomlWriter::write(std::string_view key, bool value) {
	TomlValue toml_value{false};
	toml_value.boolean() = value;
	write_value(key, std::move(toml_value));
}

TomlWriter::SectionWriteOrder &TomlWriter::write_order_for(
	std::string_view section)
{
	const auto existing = std::find_if(
		m_section_write_order.begin(),
		m_section_write_order.end(),
		[section](const auto &order) { return order.name == section; });
	if (existing != m_section_write_order.end()) return *existing;
	m_section_write_order.push_back({std::string(section), {}, {}});
	return m_section_write_order.back();
}

void TomlWriter::write_value(std::string_view key, TomlValue value) {
	if (!m_current_section) return;
	auto *section_value = document().root.find(*m_current_section);
	if (!section_value || !section_value->is<TomlTable>()) return;

	auto &order = write_order_for(*m_current_section);
	const bool first_write = !contains_name(order.seen_keys, key);
	if (first_write) {
		if (section_value->find(key)) {
			move_entries_before(section_value->table(), order.pending_keys, key);
			order.pending_keys.clear();
		} else {
			order.pending_keys.emplace_back(key);
		}
		order.seen_keys.emplace_back(key);
	}

	if (auto *existing = section_value->find(key)) {
		value.leading_comments = std::move(existing->leading_comments);
		value.trailing_comment = std::move(existing->trailing_comment);
		value.dangling_comments = std::move(existing->dangling_comments);
		*existing = std::move(value);
		return;
	}
	section_value->insert(std::string(key), std::move(value));
}

// ─── TOML serialization ──────────────────────────────────────────────────────

static void serialize_key(std::ostream &output, std::string_view key) {
	if (!key.empty() &&
	    std::all_of(key.begin(), key.end(), [](char c) { return is_bare_key_char(c); })) {
		output.write(key.data(), static_cast<std::streamsize>(key.size()));
		return;
	}
	serialize_string(output, key);
}

static std::string serialize_integer(const TomlValue &value) {
	int base = 10;
	std::string_view prefix;
	bool uppercase = false;
	switch (value.format) {
		case TomlValueFormat::IntegerHexLower:
			base = 16;
			prefix = "0x";
			break;
		case TomlValueFormat::IntegerHexUpper:
			base = 16;
			prefix = "0x";
			uppercase = true;
			break;
		case TomlValueFormat::IntegerOctal:
			base = 8;
			prefix = "0o";
			break;
		case TomlValueFormat::IntegerBinary:
			base = 2;
			prefix = "0b";
			break;
		default:
			return std::to_string(value.integer());
	}

	char buffer[65];
	const auto result = std::to_chars(
		std::begin(buffer),
		std::end(buffer),
		static_cast<uint64_t>(value.integer()),
		base);
	if (result.ec != std::errc{}) return std::to_string(value.integer());
	std::string digits(buffer, result.ptr);
	if (digits.size() < value.format_width) {
		digits.insert(0, value.format_width - digits.size(), '0');
	}
	if (uppercase) {
		std::transform(
			digits.begin(),
			digits.end(),
			digits.begin(),
			[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	}
	return std::string(prefix) + digits;
}

static std::string serialize_float(const TomlValue &toml_value) {
	const double value = toml_value.floating();
	if (std::isnan(value)) return "nan";
	if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
	if (toml_value.format == TomlValueFormat::FloatCompact) {
		auto output = std::format("{:.7g}", value);
		if (output.find_first_of(".eE") == std::string::npos) output += ".0";
		return output;
	}

	char buffer[64];
	const bool scientific =
		toml_value.format == TomlValueFormat::FloatScientificLower ||
		toml_value.format == TomlValueFormat::FloatScientificUpper;
	const auto result = std::to_chars(
		std::begin(buffer),
		std::end(buffer),
		value,
		scientific ? std::chars_format::scientific : std::chars_format::general);
	if (result.ec != std::errc{}) return "0.0";
	std::string output(buffer, result.ptr);
	if (toml_value.format == TomlValueFormat::FloatScientificUpper) {
		if (const auto exponent = output.find('e'); exponent != std::string::npos) {
			output[exponent] = 'E';
		}
	}
	if (output.find_first_of(".eE") == std::string::npos) output += ".0";
	return output;
}

static void serialize_value(std::ostream &output, const TomlValue &value);

static void append_comments(
	std::ostream &output,
	const std::vector<TomlComment> &comments,
	std::string_view indentation = {})
{
	for (const auto &comment : comments) {
		output.write(indentation.data(), static_cast<std::streamsize>(indentation.size()));
		output.put('#');
		output.write(comment.text.data(), static_cast<std::streamsize>(comment.text.size()));
		output.put('\n');
	}
}

static void append_trailing_comment(
	std::ostream &output,
	const std::optional<TomlComment> &comment)
{
	if (!comment) return;
	output.write(" #", 2);
	output.write(
		comment->text.data(),
		static_cast<std::streamsize>(comment->text.size()));
}

static void serialize_inline_table(std::ostream &output, const TomlValue &value) {
	output.put('{');
	bool first = true;
	for (const auto &[key, child] : value.table()) {
		if (!first) output.write(", ", 2);
		first = false;
		serialize_key(output, key);
		output.write(" = ", 3);
		serialize_value(output, child);
	}
	output.put('}');
}

static void serialize_value(std::ostream &output, const TomlValue &value) {
	if (value.is<std::string>()) {
		serialize_string(output, value.text());
		return;
	}
	if (value.is<int64_t>()) {
		output << serialize_integer(value);
		return;
	}
	if (value.is<double>()) {
		output << serialize_float(value);
		return;
	}
	if (value.is<bool>()) {
		output << (value.boolean() ? "true" : "false");
		return;
	}
	if (value.is<TomlOffsetDateTime>() ||
	    value.is<TomlLocalDateTime>() ||
	    value.is<TomlLocalDate>() ||
	    value.is<TomlLocalTime>()) {
		output << value.text();
		return;
	}
	if (value.is<TomlArray>()) {
		const bool has_comments =
			!value.dangling_comments.empty() ||
			std::any_of(value.array().begin(), value.array().end(), [](const TomlValue &element) {
				return !element.leading_comments.empty() || element.trailing_comment.has_value();
			});
		if (has_comments) {
			output.write("[\n", 2);
			for (size_t i = 0; i < value.array().size(); ++i) {
				const auto &element = value.array()[i];
				append_comments(output, element.leading_comments, "  ");
				output.write("  ", 2);
				serialize_value(output, element);
				if (i + 1 < value.array().size()) output.put(',');
				append_trailing_comment(output, element.trailing_comment);
				output.put('\n');
			}
			append_comments(output, value.dangling_comments, "  ");
			output.put(']');
			return;
		}
		output.put('[');
		for (size_t i = 0; i < value.array().size(); ++i) {
			if (i > 0) output.write(", ", 2);
			serialize_value(output, value.array()[i]);
		}
		output.put(']');
		return;
	}
	serialize_inline_table(output, value);
}

static bool is_table_array(const TomlValue &value) {
	return value.is<TomlArray>() && value.array_of_tables;
}

static void serialize_path(
	std::ostream &output,
	const std::vector<std::string> &path)
{
	for (size_t i = 0; i < path.size(); ++i) {
		if (i > 0) output.put('.');
		serialize_key(output, path[i]);
	}
}

static void append_blank_line(std::ostream &output, bool has_output) {
	if (has_output) output.put('\n');
}

static bool has_direct_values(const TomlValue &table) {
	return std::any_of(
		table.table().begin(),
		table.table().end(),
		[](const std::pair<std::string, TomlValue> &entry) {
			const auto &value = entry.second;
			return (!value.is<TomlTable>() || value.inline_table) &&
			       !is_table_array(value);
		});
}

static void serialize_dotted_assignments(
	std::ostream &output,
	bool &has_output,
	const TomlValue &table,
	const std::vector<std::string> &prefix)
{
	for (const auto &[key, value] : table.table()) {
		auto dotted_path = prefix;
		dotted_path.push_back(key);
		if (value.is<TomlTable>() && !value.inline_table) {
			if (value.dotted_table) {
				serialize_dotted_assignments(
					output,
					has_output,
					value,
					dotted_path);
			}
			continue;
		}
		if (is_table_array(value)) continue;

		append_comments(output, value.leading_comments);
		serialize_path(output, dotted_path);
		output.write(" = ", 3);
		serialize_value(output, value);
		append_trailing_comment(output, value.trailing_comment);
		output.put('\n');
		has_output = true;
	}
}

static void serialize_table_contents(
	std::ostream &output,
	bool &has_output,
	const TomlValue &table,
	const std::vector<std::string> &path);

static void serialize_child_tables(
	std::ostream &output,
	bool &has_output,
	const TomlValue &table,
	const std::vector<std::string> &path)
{
	for (const auto &[key, value] : table.table()) {
		auto child_path = path;
		child_path.push_back(key);
		if (value.is<TomlTable>() && !value.inline_table) {
			if (value.dotted_table) {
				serialize_child_tables(output, has_output, value, child_path);
				continue;
			}
			const bool needs_header =
				value.explicit_table ||
				has_direct_values(value);
			if (needs_header) {
				append_blank_line(output, has_output);
				append_comments(output, value.leading_comments);
				output.put('[');
				serialize_path(output, child_path);
				output.put(']');
				append_trailing_comment(output, value.trailing_comment);
				output.put('\n');
				has_output = true;
			}
			serialize_table_contents(output, has_output, value, child_path);
		} else if (is_table_array(value)) {
			for (const auto &element : value.array()) {
				append_blank_line(output, has_output);
				append_comments(output, element.leading_comments);
				output.write("[[", 2);
				serialize_path(output, child_path);
				output.write("]]", 2);
				append_trailing_comment(output, element.trailing_comment);
				output.put('\n');
				has_output = true;
				serialize_table_contents(output, has_output, element, child_path);
			}
		}
	}
}

static void serialize_table_contents(
	std::ostream &output,
	bool &has_output,
	const TomlValue &table,
	const std::vector<std::string> &path)
{
	for (const auto &[key, value] : table.table()) {
		if (value.is<TomlTable>() && !value.inline_table) {
			if (value.dotted_table) {
				serialize_dotted_assignments(
					output,
					has_output,
					value,
					{key});
			}
			continue;
		}
		if (is_table_array(value)) continue;
		append_comments(output, value.leading_comments);
		serialize_key(output, key);
		output.write(" = ", 3);
		serialize_value(output, value);
		append_trailing_comment(output, value.trailing_comment);
		output.put('\n');
		has_output = true;
	}
	serialize_child_tables(output, has_output, table, path);
}

std::string toml::to_string(const TomlDocument &document) {
	std::ostringstream output;
	to_stream(document, output);
	return std::move(output).str();
}

bool toml::to_file(
	const TomlDocument &document,
	const std::filesystem::path &path)
{
	std::ofstream f(path, std::ios::out | std::ios::binary);
	if (!f) return false;
	const bool serialized = to_stream(document, f);
	f.close();
	return serialized && f.good();
}

bool toml::to_stream(
	const TomlDocument &document,
	std::ostream &output)
{
	bool has_output = false;
	serialize_table_contents(output, has_output, document.root, {});
	append_comments(output, document.trailing_comments);
	return output.good();
}
