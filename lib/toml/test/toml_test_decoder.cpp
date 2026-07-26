// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#include "toml.h"

#include <charconv>
#include <cmath>
#include <iostream>
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

void write_value(std::ostream &output, const TomlValue &value);

void write_table(std::ostream &output, const TomlValue &value) {
	output.put('{');
	bool first = true;
	for (const auto &[key, child] : value.table()) {
		if (!first) output.put(',');
		first = false;
		write_json_string(output, key);
		output.put(':');
		write_value(output, child);
	}
	output.put('}');
}

void write_value(std::ostream &output, const TomlValue &value) {
	if (value.is<TomlTable>()) {
		write_table(output, value);
		return;
	}
	if (value.is<TomlArray>()) {
		output.put('[');
		bool first = true;
		for (const auto &element : value.array()) {
			if (!first) output.put(',');
			first = false;
			write_value(output, element);
		}
		output.put(']');
		return;
	}

	output << R"({"type":")";
	if (value.is<std::string>()) {
		output << R"(string","value":)";
		write_json_string(output, value.text());
	} else if (value.is<int64_t>()) {
		output << R"(integer","value":")" << value.integer() << '"';
	} else if (value.is<double>()) {
		output << R"(float","value":)";
		write_json_string(output, float_string(value.floating()));
	} else if (value.is<bool>()) {
		output << R"(bool","value":")" << (value.boolean() ? "true" : "false") << '"';
	} else if (value.is<TomlOffsetDateTime>()) {
		output << R"(datetime","value":)";
		write_json_string(output, value.text());
	} else if (value.is<TomlLocalDateTime>()) {
		output << R"(datetime-local","value":)";
		write_json_string(output, value.text());
	} else if (value.is<TomlLocalDate>()) {
		output << R"(date-local","value":)";
		write_json_string(output, value.text());
	} else if (value.is<TomlLocalTime>()) {
		output << R"(time-local","value":)";
		write_json_string(output, value.text());
	}
	output.put('}');
}

} // namespace

int main() {
	TomlReader reader;
	if (!reader.load(std::cin)) {
		std::cerr << reader.error_message() << '\n';
		return 1;
	}

	write_table(std::cout, reader.document().root);
	std::cout.put('\n');
	return std::cout ? 0 : 1;
}
