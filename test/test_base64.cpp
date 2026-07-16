#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../src/util/coder/base64.h"

static std::span<const std::byte> as_bytes(std::string_view value) {
	return std::as_bytes(std::span(value.data(), value.size()));
}

static std::string as_string(const std::vector<std::byte> &value) {
	if(value.empty()) return {};
	return {reinterpret_cast<const char*>(value.data()), value.size()};
}

TEST_CASE("base64 matches RFC 4648 test vectors", "[base64]") {
	// RFC 4648 section 10.
	constexpr std::array vectors = {
		std::pair{"",       ""},
		std::pair{"f",      "Zg=="},
		std::pair{"fo",     "Zm8="},
		std::pair{"foo",    "Zm9v"},
		std::pair{"foob",   "Zm9vYg=="},
		std::pair{"fooba",  "Zm9vYmE="},
		std::pair{"foobar", "Zm9vYmFy"},
	};

	for(const auto &[plain, encoded] : vectors) {
		CAPTURE(plain, encoded);
		CHECK(base64_encode(as_bytes(plain)) == encoded);
		auto decoded = base64_decode(encoded);
		REQUIRE(decoded.has_value());
		CHECK(as_string(*decoded) == plain);
	}
}

TEST_CASE("base64 round-trips arbitrary binary bytes", "[base64]") {
	std::vector<std::byte> input;
	for(unsigned value = 0; value <= 0xff; ++value) {
		input.push_back(static_cast<std::byte>(value));
	}

	const auto encoded = base64_encode(input);
	const auto decoded = base64_decode(encoded);
	REQUIRE(decoded.has_value());
	CHECK(*decoded == input);
}

TEST_CASE("base64 rejects malformed encodings", "[base64]") {
	constexpr std::array invalid = {
		"A", "AAA", "AAAAA",          // length is not a multiple of four
		"=AAA", "A=AA", "AA=A",       // padding in an invalid position
		"AA==AAAA", "AAAA=AAA",         // padding before the final quartet
		"AA*A", "AA-A", "AA_A",       // invalid and URL-safe characters
		"Z g==", "Zg==\n",              // whitespace is not accepted
		"Zh==", "Zm9=",                 // non-zero bits hidden by padding
	};

	for(const auto value : invalid) {
		CAPTURE(value);
		CHECK_FALSE(base64_decode(value).has_value());
	}
}

TEST_CASE("base64 accepts valid padding forms", "[base64]") {
	CHECK(as_string(*base64_decode("AA==")) == std::string("\0", 1));
	CHECK(as_string(*base64_decode("AAA=")) == std::string("\0\0", 2));
	CHECK(as_string(*base64_decode("AAAA")) == std::string("\0\0\0", 3));
}

TEST_CASE("base64 encoding appends to an existing string", "[base64]") {
	std::string encoded = "prefix:";
	base64_encode_append(encoded, as_bytes("foobar"));
	CHECK(encoded == "prefix:Zm9vYmFy");
}
