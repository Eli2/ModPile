#include <catch2/catch_test_macros.hpp>

#include "../src/util/hash_util.h"

static std::string MD5(std::string s) {
	auto bytes = std::as_bytes(std::span{s.data(), s.size()});
	return calc_md5(bytes);
}

// https://datatracker.ietf.org/doc/html/rfc1321
TEST_CASE("MD5 Test 1 empty", "[hash]") {
	REQUIRE(MD5("") == "d41d8cd98f00b204e9800998ecf8427e");
}
TEST_CASE("MD5 Test 2 a", "[hash]") {
	REQUIRE(MD5("a") == "0cc175b9c0f1b6a831c399e269772661");
}
TEST_CASE("MD5 Test 3 abc", "[hash]") {
	REQUIRE(MD5("abc") == "900150983cd24fb0d6963f7d28e17f72");
}
TEST_CASE("MD5 Test 4", "[hash]") {
	REQUIRE(MD5("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
}
TEST_CASE("MD5 Test 5", "[hash]") {
	REQUIRE(MD5("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
}
TEST_CASE("MD5 Test 6", "[hash]") {
	REQUIRE(MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == "d174ab98d277d9f5a5611c2c9f419d9f");
}
TEST_CASE("MD5 Test 7", "[hash]") {
	REQUIRE(MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890") == "57edf4a22be3c955ac49da2e2107b67a");
}
TEST_CASE("MD5 Test unicode", "[hash]") {
	REQUIRE(MD5("你好吗？") == "bb0b6bc45375143826f72439e050743e");
}

static std::string SHA1(std::string s) {
	auto bytes = std::as_bytes(std::span{s.data(), s.size()});
	return calc_sha1(bytes);
}

TEST_CASE("SHA-1 empty", "[hash]") {
	REQUIRE(SHA1("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}
TEST_CASE("SHA-1 abc", "[hash]") {
	REQUIRE(SHA1("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
}
TEST_CASE("SHA-1 1 million a", "[hash]") {
	std::string in = std::string(1'000'000, 'a');
	REQUIRE(SHA1(in) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}
