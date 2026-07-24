#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "../src/util/toml.h"

namespace {

TomlReader read_toml(const std::string &text) {
	std::istringstream input(text);
	TomlReader reader;
	REQUIRE(reader.load(input));
	return reader;
}

} // namespace

TEST_CASE("TOML reader reads every supported scalar type", "[toml][reader]") {
	auto reader = read_toml(R"(
		[values]
		text = "hello"
		integer = -42
		hex_lower = 0x2a
		hex_upper = 0XCAFE
		fraction = -12.5
		exponent = 5e+2
		enabled = true
		disabled = false
	)");

	CHECK(reader.get_string("values", "text") == "hello");
	CHECK(reader.get_integer("values", "integer") == -42);
	CHECK(reader.get_integer("values", "hex_lower") == 42);
	CHECK(reader.get_integer("values", "hex_upper") == 0xCAFE);
	CHECK(reader.get_float("values", "fraction") == -12.5);
	CHECK(reader.get_float("values", "exponent") == 500.0);
	CHECK(reader.get_bool("values", "enabled") == true);
	CHECK(reader.get_bool("values", "disabled") == false);
}

TEST_CASE("TOML reader accepts integers where callers request floats", "[toml][reader]") {
	auto reader = read_toml("[player]\ngain = 2\n");

	CHECK(reader.get_float("player", "gain") == 2.0);
	CHECK(reader.get_integer("player", "gain") == 2);
}

TEST_CASE("TOML reader keeps scalar types distinct", "[toml][reader]") {
	auto reader = read_toml(R"(
		[values]
		text = "1"
		integer = 1
		float = 1.5
		boolean = true
	)");

	CHECK_FALSE(reader.get_integer("values", "text"));
	CHECK_FALSE(reader.get_bool("values", "integer"));
	CHECK_FALSE(reader.get_integer("values", "float"));
	CHECK_FALSE(reader.get_string("values", "boolean"));
}

TEST_CASE("TOML reader handles whitespace CRLF and comments", "[toml][reader]") {
	auto reader = read_toml(
		"# full-line comment\r\n"
		"  [ spaced ]  # section comment\r\n"
		"  key \t=\t \"value # not a comment\"  # value comment\r\n"
		"number = 7 # numeric comment\r\n"
		"flag = true # boolean comment\r\n");

	CHECK(reader.get_string("spaced", "key") == "value # not a comment");
	CHECK(reader.get_integer("spaced", "number") == 7);
	CHECK(reader.get_bool("spaced", "flag") == true);
}

TEST_CASE("TOML reader decodes supported basic-string escapes", "[toml][reader]") {
	auto reader = read_toml(
		R"([strings]
value = "quote: \" slash: \\ newline:\n tab:\t carriage:\r backspace:\b formfeed:\f"
)");

	CHECK(reader.get_string("strings", "value") ==
	      "quote: \" slash: \\ newline:\n tab:\t carriage:\r backspace:\b formfeed:\f");
}

TEST_CASE("TOML reader preserves UTF-8 strings", "[toml][reader]") {
	auto reader = read_toml("[strings]\nvalue = \"Grüße 日本語 🎛️\"\n");
	CHECK(reader.get_string("strings", "value") == "Grüße 日本語 🎛️");
}

TEST_CASE("TOML reader handles signed values exponents and negative zero", "[toml][reader]") {
	auto reader = read_toml(R"(
		[numbers]
		positive = +17
		negative = -17
		fraction = +1.25
		lower_exponent = 1e3
		upper_exponent = -2E-2
		negative_zero = -0.0
	)");

	CHECK(reader.get_integer("numbers", "positive") == 17);
	CHECK(reader.get_integer("numbers", "negative") == -17);
	CHECK(reader.get_float("numbers", "fraction") == 1.25);
	CHECK(reader.get_float("numbers", "lower_exponent") == 1000.0);
	CHECK(reader.get_float("numbers", "upper_exponent") == -0.02);
	const auto negative_zero = reader.get_float("numbers", "negative_zero");
	REQUIRE(negative_zero);
	CHECK(std::signbit(*negative_zero));
}

TEST_CASE("TOML reader skips malformed and out-of-scope input", "[toml][reader]") {
	auto reader = read_toml(R"(
		root = 1
		broken section
		[valid]
		= 1
		missing_equals
		unterminated = "value
		bad_integer = 12oops
		bad_float = 1.2.3
		bad_boolean = TRUE
		good = 9
	)");

	CHECK_FALSE(reader.get_integer("", "root"));
	CHECK_FALSE(reader.get_integer("valid", ""));
	CHECK_FALSE(reader.get_string("valid", "unterminated"));
	CHECK_FALSE(reader.get_integer("valid", "bad_integer"));
	CHECK_FALSE(reader.get_float("valid", "bad_float"));
	CHECK_FALSE(reader.get_bool("valid", "bad_boolean"));
	CHECK(reader.get_integer("valid", "good") == 9);
}

TEST_CASE("TOML reader separates sections and missing values", "[toml][reader]") {
	auto reader = read_toml("[first]\nvalue = 1\n[second]\nvalue = 2\n");

	CHECK(reader.get_integer("first", "value") == 1);
	CHECK(reader.get_integer("second", "value") == 2);
	CHECK_FALSE(reader.get_integer("third", "value"));
	CHECK_FALSE(reader.get_integer("first", "missing"));
}

TEST_CASE("TOML reader replaces its state on a subsequent load", "[toml][reader]") {
	TomlReader reader;
	std::istringstream first("[section]\nold = 1\n");
	REQUIRE(reader.load(first));
	std::istringstream second("[section]\nnew = 2\n");
	REQUIRE(reader.load(second));

	CHECK_FALSE(reader.get_integer("section", "old"));
	CHECK(reader.get_integer("section", "new") == 2);
}

TEST_CASE("TOML writer serializes supported values in call order", "[toml][writer]") {
	TomlWriter writer;
	writer.section("first");
	writer.write("text", std::string_view("hello"));
	writer.write("integer", int{-7});
	writer.write_hex("hex", uint16_t{0x2a});
	writer.write("float", float{1.25});
	writer.write("enabled", true);
	writer.section("second");
	writer.write("disabled", false);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "[first]\n"
	      "text = \"hello\"\n"
	      "integer = -7\n"
	      "hex = 0x002a\n"
	      "float = 1.25\n"
	      "enabled = true\n"
	      "\n"
	      "[second]\n"
	      "disabled = false\n");
}

TEST_CASE("TOML writer escapes basic strings and round-trips them", "[toml][writer]") {
	const std::string expected = "quote \" slash \\ newline\n tab\t carriage\r";
	TomlWriter writer;
	writer.section("strings");
	writer.write("value", expected);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() == "[strings]\nvalue = \"quote \\\" slash \\\\ newline\\n tab\\t carriage\\r\"\n");
	std::istringstream input(output.str());
	TomlReader reader;
	REQUIRE(reader.load(input));
	CHECK(reader.get_string("strings", "value") == expected);
}

TEST_CASE("TOML writer preserves a human-edited document", "[toml][writer]") {
	const std::string original =
		"# Personal settings; keep this comment\n"
		"[player] # playback controls\n"
		"gain   = 1.0    # adjusted by the volume knob\n"
		"theme = \"midnight\" # setting unknown to ModPile\n"
		"\n"
		"[library]\n"
		"paths = [\"~/Music\", \"/mnt/modules\"]\n";
	std::istringstream input(original);
	TomlWriter writer;
	REQUIRE(writer.load(input));

	writer.section("player");
	writer.write("gain", 0.75);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "# Personal settings; keep this comment\n"
	      "[player] # playback controls\n"
	      "gain   = 0.75    # adjusted by the volume knob\n"
	      "theme = \"midnight\" # setting unknown to ModPile\n"
	      "\n"
	      "[library]\n"
	      "paths = [\"~/Music\", \"/mnt/modules\"]\n");
}

TEST_CASE("TOML writer preserves CRLF and appends missing keys", "[toml][writer]") {
	std::istringstream input("[player]\r\ngain = 1\r\n\r\n[other]\r\nvalue = true\r\n");
	TomlWriter writer;
	REQUIRE(writer.load(input));

	writer.section("player");
	writer.write("stereo_width", 0.5);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "[player]\r\n"
	      "gain = 1\r\n"
	      "stereo_width = 0.5\r\n"
	      "\r\n"
	      "[other]\r\n"
	      "value = true\r\n");
}

TEST_CASE("TOML stream operations report I/O errors", "[toml][stream]") {
	std::istringstream input("[section]\nvalue = 1\n");
	input.setstate(std::ios::badbit);
	TomlReader reader;
	CHECK_FALSE(reader.load(input));

	TomlWriter writer;
	writer.section("section");
	std::ostringstream output;
	output.setstate(std::ios::badbit);
	CHECK_FALSE(writer.save(output));
}
