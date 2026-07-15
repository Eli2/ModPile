// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "xml_util.h"

#include <cctype>
#include <format>
#include <optional>

static bool xml_name_char(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) ||
		c == '_' || c == '-' || c == ':' || c == '.';
}

static void skip_xml_space(std::string_view xml, size_t &pos, size_t end) {
	while(pos < end && std::isspace(static_cast<unsigned char>(xml[pos]))) {
		pos++;
	}
}

static std::optional<size_t> find_xml_tag_end(std::string_view xml, size_t tag_start) {
	char quote = 0;
	for(size_t pos = tag_start + 1; pos < xml.size(); pos++) {
		if(quote) {
			if(xml[pos] == quote) {
				quote = 0;
			}
			continue;
		}
		if(xml[pos] == '\'' || xml[pos] == '"') {
			quote = xml[pos];
			continue;
		}
		if(xml[pos] == '>') {
			return pos;
		}
	}
	return std::nullopt;
}

static std::optional<size_t> find_xml_ignored_tag_end(std::string_view xml, size_t tag_start) {
	if(xml.substr(tag_start, 4) == "<!--") {
		const size_t comment_end = xml.find("-->", tag_start + 4);
		if(comment_end == std::string_view::npos) {
			return std::nullopt;
		}
		return comment_end + 3;
	}

	if(xml.substr(tag_start, 2) == "<?") {
		const size_t pi_end = xml.find("?>", tag_start + 2);
		if(pi_end == std::string_view::npos) {
			return std::nullopt;
		}
		return pi_end + 2;
	}

	if(tag_start + 1 < xml.size() && (xml[tag_start + 1] == '!' || xml[tag_start + 1] == '/')) {
		auto tag_end = find_xml_tag_end(xml, tag_start);
		if(!tag_end) {
			return std::nullopt;
		}
		return tag_end.value() + 1;
	}

	return std::nullopt;
}

static bool find_xml_attribute(
	std::string_view xml,
	size_t tag_start,
	size_t tag_end,
	std::string_view name,
	size_t &value_begin,
	size_t &value_end,
	char &quote)
{
	size_t pos = tag_start + 1;
	skip_xml_space(xml, pos, tag_end);
	while(pos < tag_end && xml_name_char(xml[pos])) {
		pos++;
	}

	while(pos < tag_end) {
		skip_xml_space(xml, pos, tag_end);
		if(pos >= tag_end || xml[pos] == '/') {
			break;
		}

		const size_t name_begin = pos;
		while(pos < tag_end && xml_name_char(xml[pos])) {
			pos++;
		}
		if(name_begin == pos) {
			break;
		}
		const auto attr_name = xml.substr(name_begin, pos - name_begin);

		skip_xml_space(xml, pos, tag_end);
		if(pos >= tag_end || xml[pos] != '=') {
			continue;
		}
		pos++;
		skip_xml_space(xml, pos, tag_end);
		if(pos >= tag_end || (xml[pos] != '\'' && xml[pos] != '"')) {
			return false;
		}

		quote = xml[pos];
		value_begin = pos + 1;
		value_end = xml.find(quote, value_begin);
		if(value_end == std::string_view::npos || value_end > tag_end) {
			return false;
		}

		if(attr_name == name) {
			return true;
		}
		pos = value_end + 1;
	}

	return false;
}

static std::string xml_escape_attribute(std::string_view value, char quote) {
	std::string escaped;
	for(char c : value) {
		switch(c) {
		case '&': escaped += "&amp;"; break;
		case '<': escaped += "&lt;"; break;
		case '>': escaped += "&gt;"; break;
		case '\'':
			escaped += quote == '\'' ? "&apos;" : "'";
			break;
		case '"':
			escaped += quote == '"' ? "&quot;" : "\"";
			break;
		default:
			escaped += c;
			break;
		}
	}
	return escaped;
}

bool set_xml_attribute_by_id(
	std::string &xml,
	std::string_view id,
	std::string_view attribute,
	std::string_view value)
{
	size_t pos = 0;
	while((pos = xml.find('<', pos)) != std::string::npos) {
		if(pos + 1 >= xml.size()) {
			return false;
		}

		if(auto ignored_end = find_xml_ignored_tag_end(xml, pos)) {
			pos = ignored_end.value();
			continue;
		}

		auto tag_end_opt = find_xml_tag_end(xml, pos);
		if(!tag_end_opt) {
			return false;
		}
		const size_t tag_end = tag_end_opt.value();

		size_t id_begin = 0;
		size_t id_end = 0;
		char id_quote = 0;
		if(find_xml_attribute(xml, pos, tag_end, "id", id_begin, id_end, id_quote) &&
		   std::string_view(xml).substr(id_begin, id_end - id_begin) == id)
		{
			size_t value_begin = 0;
			size_t value_end = 0;
			char value_quote = 0;
			if(find_xml_attribute(xml, pos, tag_end, attribute, value_begin, value_end, value_quote)) {
				xml.replace(value_begin, value_end - value_begin, xml_escape_attribute(value, value_quote));
			} else {
				size_t insert_pos = tag_end;
				while(insert_pos > pos && std::isspace(static_cast<unsigned char>(xml[insert_pos - 1]))) {
					insert_pos--;
				}
				if(insert_pos > pos && xml[insert_pos - 1] == '/') {
					insert_pos--;
					while(insert_pos > pos && std::isspace(static_cast<unsigned char>(xml[insert_pos - 1]))) {
						insert_pos--;
					}
				}
				xml.insert(insert_pos, std::format(" {}='{}'", attribute, xml_escape_attribute(value, '\'')));
			}
			return true;
		}

		pos = tag_end + 1;
	}

	return false;
}
