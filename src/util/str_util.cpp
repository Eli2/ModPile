// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "str_util.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <string>
#include <vector>


std::string_view load_string(const char * data, size_t maxLength) {
	return std::string_view(data, std::find(data, data + maxLength, '\0') - data);
}

bool is_empty_or_whitespace(const std::string_view &str) {
	return str.empty() || std::all_of(str.begin(), str.end(), [](char c) {
		return std::isspace(c);
	});
}

std::string toUtf8(const std::wstring& wide) {
	const std::locale locale("");
	const auto& facet = std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(locale);
	
	std::mbstate_t state{};
	std::string result(wide.size() * 4, '\0');
	const wchar_t* from_next;
	char* to_next;
	
	facet.out(state, wide.data(), wide.data() + wide.size(), from_next,
			  result.data(), result.data() + result.size(), to_next);
	result.resize(to_next - result.data());
	return result;
}

// trim from start (in place)
inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](char ch) {
		return !std::isspace(ch);
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

