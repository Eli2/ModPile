// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// RFC 4648 base64 with required padding and no embedded whitespace.
std::string base64_encode(std::span<const std::byte> data);
void base64_encode_append(std::string &out, std::span<const std::byte> data);
std::optional<std::vector<std::byte>> base64_decode(std::string_view encoded);
