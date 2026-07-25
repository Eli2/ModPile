#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "ordered_map.h"

TEST_CASE("OrderedMap provides map lookup with insertion-order iteration", "[toml][ordered-map]") {
	OrderedMap<std::string, int> values;

	const auto [first, inserted_first] = values.try_emplace("first", 1);
	const auto [second, inserted_second] = values.try_emplace("second", 2);
	const auto [duplicate, inserted_duplicate] = values.try_emplace("first", 100);

	CHECK(inserted_first);
	CHECK(inserted_second);
	CHECK_FALSE(inserted_duplicate);
	CHECK(first->second == 1);
	CHECK(second->second == 2);
	CHECK(duplicate->second == 1);
	REQUIRE(values.size() == 2);
	CHECK(values.begin()->first == "first");
	CHECK(std::next(values.begin())->first == "second");

	const std::string_view second_key = "second";
	CHECK(values.contains(second_key));
	CHECK(values.count(second_key) == 1);
	CHECK(values.find(second_key)->second == 2);
	CHECK(values.at(second_key) == 2);
	CHECK_THROWS_AS(values.at("missing"), std::out_of_range);

	using KeyReference = decltype(values.begin()->first);
	STATIC_CHECK(std::is_const_v<std::remove_reference_t<KeyReference>>);
}

TEST_CASE("OrderedMap updates and erases without losing its index", "[toml][ordered-map]") {
	OrderedMap<std::string, int> values;
	values["first"] = 1;
	values["second"] = 2;
	values["third"] = 3;

	const auto [updated, inserted] = values.insert_or_assign("second", 20);
	CHECK_FALSE(inserted);
	CHECK(updated->second == 20);
	CHECK(std::next(values.begin())->first == "second");

	REQUIRE(values.erase("first") == 1);
	CHECK(values.begin()->first == "second");
	CHECK(values.find("second")->second == 20);
	CHECK(values.find("third")->second == 3);
	CHECK(values.erase("missing") == 0);

	OrderedMap<std::string, int> copy;
	copy = values;
	CHECK(copy.size() == 2);
	CHECK(copy.begin()->first == "second");
	CHECK(copy.at("third") == 3);

	OrderedMap<std::string, int> moved = std::move(copy);
	CHECK(moved.size() == 2);
	CHECK(moved.at("second") == 20);
}
