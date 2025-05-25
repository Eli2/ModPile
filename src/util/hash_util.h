// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <span>
#include <string>

std::string calc_sha1(const std::span<const std::byte> data);
std::string calc_md5(const std::span<const std::byte> data);
