// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <charconv>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {

void write_json_string(std::ostream &output, std::string_view value) {
	static constexpr char hex[] = "0123456789abcdef";

	output.put('"');
	for (const unsigned char c : value) {
		switch (c) {
			case '"':  output << "\\\""; break;
			case '\\': output << "\\\\"; break;
			case '\b': output << "\\b"; break;
			case '\f': output << "\\f"; break;
			case '\n': output << "\\n"; break;
			case '\r': output << "\\r"; break;
			case '\t': output << "\\t"; break;
			default:
				if (c < 0x20) {
					output << "\\u00" << hex[c >> 4] << hex[c & 0x0f];
				} else {
					output.put(static_cast<char>(c));
				}
		}
	}
	output.put('"');
}

std::string float_string(double value) {
	if (std::isnan(value)) {
		return "nan";
	}
	if (std::isinf(value)) {
		return std::signbit(value) ? "-inf" : "inf";
	}

	char buffer[64];
	const auto result = std::to_chars(
		std::begin(buffer),
		std::end(buffer),
		value,
		std::chars_format::general);
	if (result.ec != std::errc{}) {
		return {};
	}
	return {buffer, result.ptr};
}

void write_tagged_value(std::ostream &output, const TomlValue &value) {
	output << R"({"type":")";
	switch (value.type) {
		case TomlValue::Type::String:
			output << R"(string","value":)";
			write_json_string(output, value.str);
			break;
		case TomlValue::Type::Integer:
			output << R"(integer","value":")" << value.i << '"';
			break;
		case TomlValue::Type::Float:
			output << R"(float","value":)";
			write_json_string(output, float_string(value.f));
			break;
		case TomlValue::Type::Bool:
			output << R"(bool","value":")" << (value.b ? "true" : "false") << '"';
			break;
	}
	output.put('}');
}

} // namespace

int main() {
	TomlReader reader;
	if (!reader.load(std::cin)) {
		std::cerr << "failed to read TOML input\n";
		return 1;
	}

	using Table = std::map<std::string, TomlValue>;
	std::map<std::string, Table> tables;
	for (const auto &section : reader.sections()) {
		tables.try_emplace(section);
	}
	for (const auto &entry : reader.entries()) {
		tables[entry.section][entry.key] = entry.value;
	}

	std::cout.put('{');
	bool first_table = true;
	for (const auto &[section, values] : tables) {
		if (!first_table) {
			std::cout.put(',');
		}
		first_table = false;
		write_json_string(std::cout, section);
		std::cout << ":{";

		bool first_value = true;
		for (const auto &[key, value] : values) {
			if (!first_value) {
				std::cout.put(',');
			}
			first_value = false;
			write_json_string(std::cout, key);
			std::cout.put(':');
			write_tagged_value(std::cout, value);
		}
		std::cout.put('}');
	}
	std::cout << "}\n";
	return std::cout ? 0 : 1;
}
