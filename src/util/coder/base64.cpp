// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "base64.h"

#include <array>
#include <cstdint>

namespace {

constexpr std::string_view kAlphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const std::array<int8_t, 256> kDecodeValues = [] {
	std::array<int8_t, 256> values;
	values.fill(-1);
	for(size_t i = 0; i < kAlphabet.size(); ++i) {
		values[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int8_t>(i);
	}
	return values;
}();

} // namespace

std::string base64_encode(std::span<const std::byte> data) {
	std::string encoded;
	encoded.reserve(((data.size() + 2) / 3) * 4);

	for(size_t i = 0; i < data.size(); i += 3) {
		const uint32_t value = uint32_t(std::to_integer<uint8_t>(data[i])) << 16 |
			(i + 1 < data.size() ? uint32_t(std::to_integer<uint8_t>(data[i + 1])) << 8 : 0) |
			(i + 2 < data.size() ? uint32_t(std::to_integer<uint8_t>(data[i + 2])) : 0);
		encoded += kAlphabet[(value >> 18) & 0x3f];
		encoded += kAlphabet[(value >> 12) & 0x3f];
		encoded += i + 1 < data.size() ? kAlphabet[(value >> 6) & 0x3f] : '=';
		encoded += i + 2 < data.size() ? kAlphabet[value & 0x3f] : '=';
	}
	return encoded;
}

std::optional<std::vector<std::byte>> base64_decode(std::string_view encoded) {
	if(encoded.size() % 4 != 0) return std::nullopt;

	std::vector<std::byte> decoded;
	decoded.reserve((encoded.size() / 4) * 3);
	for(size_t i = 0; i < encoded.size(); i += 4) {
		const bool pad2 = encoded[i + 2] == '=';
		const bool pad3 = encoded[i + 3] == '=';
		if((pad2 && !pad3) || (i + 4 != encoded.size() && (pad2 || pad3))) return std::nullopt;
		if(encoded[i] == '=' || encoded[i + 1] == '=') return std::nullopt;

		const auto a = kDecodeValues[static_cast<unsigned char>(encoded[i])];
		const auto b = kDecodeValues[static_cast<unsigned char>(encoded[i + 1])];
		const auto c = pad2 ? 0 : kDecodeValues[static_cast<unsigned char>(encoded[i + 2])];
		const auto d = pad3 ? 0 : kDecodeValues[static_cast<unsigned char>(encoded[i + 3])];
		if(a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
		if((pad2 && (b & 0x0f) != 0) || (pad3 && !pad2 && (c & 0x03) != 0)) return std::nullopt;

		const uint32_t value = uint32_t(a) << 18 | uint32_t(b) << 12 |
			uint32_t(c) << 6 | uint32_t(d);
		decoded.push_back(std::byte((value >> 16) & 0xff));
		if(!pad2) decoded.push_back(std::byte((value >> 8) & 0xff));
		if(!pad3) decoded.push_back(std::byte(value & 0xff));
	}
	return decoded;
}
