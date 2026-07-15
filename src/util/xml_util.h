// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <string>
#include <string_view>

bool set_xml_attribute_by_id(
	std::string &xml,
	std::string_view id,
	std::string_view attribute,
	std::string_view value);
