#include <catch2/catch_test_macros.hpp>

#include "../src/util/str_util.h"


TEST_CASE("toUtf8 empty string", "[str]") {
	REQUIRE(toUtf8(L"") == "");
}

TEST_CASE("toUtf8 ascii", "[str]") {
	REQUIRE(toUtf8(L"hello") == "hello");
	REQUIRE(toUtf8(L"Hello, World!") == "Hello, World!");
	REQUIRE(toUtf8(L"abc 123 !@#") == "abc 123 !@#");
}

TEST_CASE("toUtf8 latin extended", "[str]") {
	// é U+00E9 → UTF-8: 0xC3 0xA9
	REQUIRE(toUtf8(L"\u00E9") == "\xC3\xA9");
	// ü U+00FC → UTF-8: 0xC3 0xBC
	REQUIRE(toUtf8(L"\u00FC") == "\xC3\xBC");
	// ñ U+00F1 → UTF-8: 0xC3 0xB1
	REQUIRE(toUtf8(L"\u00F1") == "\xC3\xB1");
}

TEST_CASE("toUtf8 mixed ascii and multibyte", "[str]") {
	// "café" → "caf\xC3\xA9"
	REQUIRE(toUtf8(L"caf\u00E9") == "caf\xC3\xA9");
	// "Ärger" → "\xC3\x84rger"
	REQUIRE(toUtf8(L"\u00C4rger") == "\xC3\x84rger");
}

TEST_CASE("toUtf8 cjk characters", "[str]") {
	// 日 U+65E5 → UTF-8: 0xE6 0x97 0xA5
	REQUIRE(toUtf8(L"\u65E5") == "\xE6\x97\xA5");
	// 本 U+672C → UTF-8: 0xE6 0x9C 0xAC
	REQUIRE(toUtf8(L"\u672C") == "\xE6\x9C\xAC");
}

TEST_CASE("Split first test 1", "[str]") {
	auto res = split_first("a b", ' ');
	
	REQUIRE(res.first == "a");
	REQUIRE(res.second == "b");
}
