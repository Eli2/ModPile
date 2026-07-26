// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct JsonValue {
	enum class Type { Object, Array, String };
	explicit JsonValue(Type value_type = Type::Object) : type(value_type) {}

	Type type;
	std::string string;
	std::vector<JsonValue> array;
	std::vector<std::pair<std::string, JsonValue>> object;

	const JsonValue *find(std::string_view key) const {
		for (const auto &[candidate, value] : object) {
			if (candidate == key) return &value;
		}
		return nullptr;
	}
};

class JsonParser {
public:
	explicit JsonParser(std::string_view input) : m_input(input) {}

	bool parse(JsonValue &value) {
		skip_whitespace();
		if (!parse_value(value)) return false;
		skip_whitespace();
		return m_pos == m_input.size();
	}

private:
	std::string_view m_input;
	size_t m_pos = 0;

	char peek(size_t offset = 0) const {
		return m_pos + offset < m_input.size() ? m_input[m_pos + offset] : '\0';
	}
	void skip_whitespace() {
		while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++m_pos;
	}
	bool parse_value(JsonValue &value) {
		if (peek() == '{') return parse_object(value);
		if (peek() == '[') return parse_array(value);
		if (peek() == '"') {
			value = JsonValue{JsonValue::Type::String};
			return parse_string(value.string);
		}
		return false;
	}
	bool parse_object(JsonValue &value) {
		value = JsonValue{JsonValue::Type::Object};
		++m_pos;
		skip_whitespace();
		if (peek() == '}') {
			++m_pos;
			return true;
		}
		while (true) {
			std::string key;
			if (!parse_string(key)) return false;
			for (const auto &[existing, child] : value.object) {
				(void)child;
				if (existing == key) return false;
			}
			skip_whitespace();
			if (peek() != ':') return false;
			++m_pos;
			skip_whitespace();
			JsonValue child;
			if (!parse_value(child)) return false;
			value.object.emplace_back(std::move(key), std::move(child));
			skip_whitespace();
			if (peek() == '}') {
				++m_pos;
				return true;
			}
			if (peek() != ',') return false;
			++m_pos;
			skip_whitespace();
		}
	}
	bool parse_array(JsonValue &value) {
		value = JsonValue{JsonValue::Type::Array};
		++m_pos;
		skip_whitespace();
		if (peek() == ']') {
			++m_pos;
			return true;
		}
		while (true) {
			JsonValue child;
			if (!parse_value(child)) return false;
			value.array.push_back(std::move(child));
			skip_whitespace();
			if (peek() == ']') {
				++m_pos;
				return true;
			}
			if (peek() != ',') return false;
			++m_pos;
			skip_whitespace();
		}
	}
	bool parse_string(std::string &output) {
		if (peek() != '"') return false;
		++m_pos;
		while (m_pos < m_input.size()) {
			const auto c = static_cast<unsigned char>(peek());
			if (c == '"') {
				++m_pos;
				return true;
			}
			if (c == '\\') {
				++m_pos;
				if (!parse_escape(output)) return false;
			} else {
				if (c < 0x20) return false;
				output.push_back(static_cast<char>(c));
				++m_pos;
			}
		}
		return false;
	}
	bool parse_escape(std::string &output) {
		switch (peek()) {
			case '"': output.push_back('"'); ++m_pos; return true;
			case '\\': output.push_back('\\'); ++m_pos; return true;
			case '/': output.push_back('/'); ++m_pos; return true;
			case 'b': output.push_back('\b'); ++m_pos; return true;
			case 'f': output.push_back('\f'); ++m_pos; return true;
			case 'n': output.push_back('\n'); ++m_pos; return true;
			case 'r': output.push_back('\r'); ++m_pos; return true;
			case 't': output.push_back('\t'); ++m_pos; return true;
			case 'u': {
				++m_pos;
				uint32_t codepoint;
				if (!parse_hex4(codepoint)) return false;
				if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
					if (peek() != '\\' || peek(1) != 'u') return false;
					m_pos += 2;
					uint32_t low;
					if (!parse_hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
					codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
				} else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
					return false;
				}
				append_utf8(output, codepoint);
				return true;
			}
			default: return false;
		}
	}
	bool parse_hex4(uint32_t &value) {
		value = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = peek();
			uint32_t digit;
			if (c >= '0' && c <= '9') digit = c - '0';
			else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
			else return false;
			value = (value << 4) | digit;
			++m_pos;
		}
		return true;
	}
	static void append_utf8(std::string &output, uint32_t codepoint) {
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
	}
};

bool decode_tagged(const JsonValue &json, TomlValue &toml) {
	if (json.type == JsonValue::Type::Array) {
		toml = TomlValue{TomlArray{}};
		for (const auto &element : json.array) {
			TomlValue converted;
			if (!decode_tagged(element, converted)) return false;
			toml.array().push_back(std::move(converted));
		}
		return true;
	}
	if (json.type != JsonValue::Type::Object) return false;

	const auto *type = json.find("type");
	const auto *value = json.find("value");
	if (json.object.size() == 2 && type && value) {
		if (type->type != JsonValue::Type::String ||
		    value->type != JsonValue::Type::String) {
			return false;
		}
		const auto &kind = type->string;
		const auto &text = value->string;
		if (kind == "string") {
			toml = TomlValue{std::string{}};
			toml.text() = text;
		} else if (kind == "integer") {
			toml = TomlValue{int64_t{}};
			const auto result = std::from_chars(
				text.data(), text.data() + text.size(), toml.integer());
			if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
		} else if (kind == "float") {
			toml = TomlValue{double{}};
			if (text == "inf" || text == "+inf") {
				toml.floating() = std::numeric_limits<double>::infinity();
			} else if (text == "-inf") {
				toml.floating() = -std::numeric_limits<double>::infinity();
			} else if (text == "nan" || text == "+nan" || text == "-nan") {
				toml.floating() = std::numeric_limits<double>::quiet_NaN();
			} else {
				const auto result = std::from_chars(
					text.data(), text.data() + text.size(), toml.floating());
				if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
			}
		} else if (kind == "bool") {
			if (text != "true" && text != "false") return false;
			toml = TomlValue{false};
			toml.boolean() = text == "true";
		} else if (kind == "datetime") {
			toml = TomlValue{TomlOffsetDateTime{}};
			toml.text() = text;
		} else if (kind == "datetime-local") {
			toml = TomlValue{TomlLocalDateTime{}};
			toml.text() = text;
		} else if (kind == "date-local") {
			toml = TomlValue{TomlLocalDate{}};
			toml.text() = text;
		} else if (kind == "time-local") {
			toml = TomlValue{TomlLocalTime{}};
			toml.text() = text;
		} else {
			return false;
		}
		return true;
	}

	toml = TomlValue{TomlTable{}};
	for (const auto &[key, child] : json.object) {
		TomlValue converted;
		if (!decode_tagged(child, converted)) return false;
		toml.insert(key, std::move(converted));
	}
	return true;
}

} // namespace

int main() {
	std::string input(
		std::istreambuf_iterator<char>(std::cin),
		std::istreambuf_iterator<char>());
	JsonValue json;
	JsonParser parser(input);
	if (!parser.parse(json) || json.type != JsonValue::Type::Object) {
		std::cerr << "invalid tagged JSON input\n";
		return 1;
	}

	TomlDocument document;
	if (!decode_tagged(json, document.root) ||
	    !document.root.is<TomlTable>()) {
		std::cerr << "invalid tagged TOML value\n";
		return 1;
	}

	TomlWriter writer;
	writer.load(document);
	if (!writer.save(std::cout)) {
		std::cerr << "failed to write TOML output\n";
		return 1;
	}
	return 0;
}
