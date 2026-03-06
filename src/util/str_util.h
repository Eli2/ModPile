// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>


template<int a, int b, int c, int d>
struct FourCC {
	static constexpr unsigned int value = (((((d << 8) | c) << 8) | b) << 8) | a;
};

std::string_view load_string(const char *data, size_t maxLength);

template <size_t N>
std::string_view load_string(const char (&data)[N]) {
	return load_string(data, N);
}
template <size_t N>
std::string_view load_string(const std::array<char, N> &data) {
	return load_string(data.data(), static_cast<size_t>(N));
}



bool is_empty_or_whitespace(const std::string_view &str);

std::string toUtf8(const std::wstring& in);

void trim(std::string &s);


template <typename callback_type_>
void split(std::string_view str, std::string_view delimiters, callback_type_ && callback) {
	std::size_t pos = 0;
	while(pos < str.size()) {
		auto const next_pos = str.find_first_of(delimiters, pos);
		callback(str.substr(pos, next_pos - pos));
		pos = next_pos == std::string_view::npos ? str.size() : next_pos + 1;
	}
}

std::vector<std::string> str_split(std::string_view str, std::string_view delimiters);

std::pair<std::string_view, std::string_view> split_first(const std::string_view in, char sep);

template <typename Range, typename Transform>
std::string str_join(const Range &range, std::string_view sep, Transform transform) {
	std::string result;
	bool first = true;
	for(const auto &item : range) {
		if(!first) result += sep;
		result += transform(item);
		first = false;
	}
	return result;
}

template <typename Range>
std::string str_join(const Range &range, std::string_view sep) {
	return str_join(range, sep, [](const auto &s) -> const auto & { return s; });
}

