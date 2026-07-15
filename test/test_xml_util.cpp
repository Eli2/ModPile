#include <catch2/catch_test_macros.hpp>

#include "../src/util/xml_util.h"

TEST_CASE("set_xml_attribute_by_id replaces single quoted attribute", "[xml]") {
	std::string xml = "<svg><rect id='seg-A' fill='#ffffff'/></svg>";

	REQUIRE(set_xml_attribute_by_id(xml, "seg-A", "fill", "transparent"));
	CHECK(xml == "<svg><rect id='seg-A' fill='transparent'/></svg>");
}

TEST_CASE("set_xml_attribute_by_id replaces double quoted attribute", "[xml]") {
	std::string xml = R"(<svg><rect fill="#ffffff" id="seg-A"/></svg>)";

	REQUIRE(set_xml_attribute_by_id(xml, "seg-A", "fill", "transparent"));
	CHECK(xml == R"(<svg><rect fill="transparent" id="seg-A"/></svg>)");
}

TEST_CASE("set_xml_attribute_by_id inserts missing attribute", "[xml]") {
	std::string xml = "<svg><rect id='seg-A'/></svg>";

	REQUIRE(set_xml_attribute_by_id(xml, "seg-A", "fill", "#ffffff"));
	CHECK(xml == "<svg><rect id='seg-A' fill='#ffffff'/></svg>");
}

TEST_CASE("set_xml_attribute_by_id escapes replacement value", "[xml]") {
	std::string xml = R"(<svg><rect id="seg-A" data="old"/></svg>)";

	REQUIRE(set_xml_attribute_by_id(xml, "seg-A", "data", R"(A&B<'"">)"));
	CHECK(xml == R"(<svg><rect id="seg-A" data="A&amp;B&lt;'&quot;&quot;&gt;"/></svg>)");
}

TEST_CASE("set_xml_attribute_by_id skips comments and processing instructions", "[xml]") {
	std::string xml = "<?xml version='1.0'?><!-- <rect id='seg-A' fill='bad'/> --><svg><rect id='seg-A' fill='old'/></svg>";

	REQUIRE(set_xml_attribute_by_id(xml, "seg-A", "fill", "new"));
	CHECK(xml == "<?xml version='1.0'?><!-- <rect id='seg-A' fill='bad'/> --><svg><rect id='seg-A' fill='new'/></svg>");
}

TEST_CASE("set_xml_attribute_by_id returns false when id is absent", "[xml]") {
	std::string xml = "<svg><rect id='seg-A' fill='old'/></svg>";

	CHECK_FALSE(set_xml_attribute_by_id(xml, "seg-B", "fill", "new"));
	CHECK(xml == "<svg><rect id='seg-A' fill='old'/></svg>");
}
