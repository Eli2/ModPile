// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "str_util.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>


std::string_view load_string(const char * data, size_t maxLength) {
	return std::string_view(data, std::find(data, data + maxLength, '\0') - data);
}

bool is_empty_or_whitespace(const std::string_view &str) {
	return str.empty() || std::all_of(str.begin(), str.end(), [](char c) {
		return std::isspace(static_cast<unsigned char>(c));
	});
}

// trim from start (in place)
inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char ch) {
		return !std::isspace(static_cast<unsigned char>(ch));
	}));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](char ch) {
		return !std::isspace(static_cast<unsigned char>(ch));
	}).base(), s.end());
}

void trim(std::string &s) {
	ltrim(s);
	rtrim(s);
}

std::vector<std::string> str_split(std::string_view str, std::string_view delimiters) {
	std::vector<std::string> result;
	split(str, delimiters, [&](const auto &s) {
		result.push_back(std::string(s));
	});
	return result;
}

std::pair<std::string_view, std::string_view> split_first(const std::string_view in, char sep) {
	auto pos = in.find(sep);
	if(pos == std::string_view::npos)
		return {in, {}};
	auto key = in.substr(0, pos);
	auto value = in.substr(pos + 1);
	return std::make_pair(key, value);
}
