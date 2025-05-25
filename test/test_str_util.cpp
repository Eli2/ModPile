#include <catch2/catch_test_macros.hpp>

#include "../src/util/str_util.h"


TEST_CASE("Split first test 1", "[str]") {
	auto res = split_first("a b", ' ');
	
	REQUIRE(res.first == "a");
	REQUIRE(res.second == "b");
}
