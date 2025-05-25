// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <string>

void curl_util_init();
void curl_util_quit();


std::string curl_util_build_url(std::string base, std::string path);
