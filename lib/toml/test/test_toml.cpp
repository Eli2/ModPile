#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "toml.h"

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
		hex_upper = 0xCAFE
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

TEST_CASE("TOML reader assigns values to application types", "[toml][reader]") {
	auto reader = read_toml(R"(
		[values]
		path = "/tmp/music"
		integer = -42
		fraction = 1.25
		enabled = true
	)");

	std::filesystem::path path;
	int integer = 0;
	std::atomic<float> fraction = 0.0f;
	bool enabled = false;

	CHECK(reader.get(path, "values", "path"));
	CHECK(reader.get(integer, "values", "integer"));
	CHECK(reader.get(fraction, "values", "fraction"));
	CHECK(reader.get(enabled, "values", "enabled"));
	CHECK(path == "/tmp/music");
	CHECK(integer == -42);
	CHECK(fraction.load() == 1.25f);
	CHECK(enabled);

	CHECK_FALSE(reader.get(integer, "values", "missing"));
	CHECK(integer == -42);
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

TEST_CASE("TOML reader preserves value section and comment order", "[toml][reader]") {
	auto reader = read_toml(
		"# document comment\n"
		"root = 1 # trailing root comment\n"
		"[z]\n"
		"first = true\n"
		"second = false\n"
		"# section comment\n"
		"[a]\n"
		"value = \"last\"\n");

	const auto &document = reader.document();
	REQUIRE(document.root.table.size() == 3);
	CHECK(document.root.table[0].first == "root");
	CHECK(document.root.table[1].first == "z");
	CHECK(document.root.table[2].first == "a");
	REQUIRE(document.root.table[1].second.table.size() == 2);
	CHECK(document.root.table[1].second.table[0].first == "first");
	CHECK(document.root.table[1].second.table[1].first == "second");

	REQUIRE(document.comments.size() == 3);
	CHECK(document.comments[0].text == " document comment");
	CHECK_FALSE(document.comments[0].trailing);
	CHECK(document.comments[1].text == " trailing root comment");
	CHECK(document.comments[1].trailing);
	CHECK(document.comments[2].text == " section comment");
	CHECK(document.comments[0].line == 1);
	CHECK(document.comments[1].line == 2);
	CHECK(document.comments[2].line == 6);
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

TEST_CASE("TOML reader rejects malformed input as a complete document", "[toml][reader]") {
	std::istringstream input(R"(
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
	TomlReader reader;
	CHECK_FALSE(reader.load(input));
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

TEST_CASE("TOML writer serializes an empty document in call order", "[toml][writer]") {
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

TEST_CASE("TOML writer canonically serializes an ordered document tree", "[toml][writer]") {
	TomlDocument document;
	document.comments.push_back({" generated document", 0, 1, 1, false});

	TomlValue title{TomlValue::Type::String};
	title.str = "demo";
	document.root.insert("title", std::move(title));

	TomlValue values{TomlValue::Type::Table};
	TomlValue enabled{TomlValue::Type::Bool};
	enabled.b = true;
	values.insert("enabled", std::move(enabled));
	TomlValue numbers{TomlValue::Type::Array};
	for (const int number : {1, 2}) {
		TomlValue element{TomlValue::Type::Integer};
		element.i = number;
		numbers.array.push_back(std::move(element));
	}
	values.insert("numbers", std::move(numbers));

	TomlValue nested{TomlValue::Type::Table};
	TomlValue date{TomlValue::Type::DateLocal};
	date.str = "2026-07-25";
	nested.insert("date", std::move(date));
	values.insert("nested", std::move(nested));
	document.root.insert("values", std::move(values));

	TomlWriter writer;
	writer.set_document(std::move(document));
	REQUIRE(writer.document().root.table[0].first == "title");
	REQUIRE(writer.document().root.table[1].first == "values");

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "# generated document\n"
	      "\n"
	      "title = \"demo\"\n"
	      "\n"
	      "[values]\n"
	      "enabled = true\n"
	      "numbers = [1, 2]\n"
	      "\n"
	      "[values.nested]\n"
	      "date = 2026-07-25\n");

	std::istringstream input(output.str());
	TomlReader reader;
	REQUIRE(reader.load(input));
	CHECK(reader.get_string("", "title") == "demo");
	CHECK(reader.get_bool("values", "enabled") == true);
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

TEST_CASE("TOML writer keeps the requested data separate from its source document", "[toml][writer]") {
	std::istringstream input("[player]\ngain = 1.0\n");
	TomlWriter writer;
	REQUIRE(writer.load(input));

	writer.section("player");
	writer.write("gain", 0.75);
	writer.write("gain", 0.5);

	std::ostringstream first;
	std::ostringstream second;
	REQUIRE(writer.save(first));
	REQUIRE(writer.save(second));
	CHECK(first.str() == "[player]\ngain = 0.5\n");
	CHECK(second.str() == first.str());
}

TEST_CASE("TOML writer reconciles the file that exists when it is saved", "[toml][writer]") {
	const auto path = std::filesystem::temp_directory_path() / "modpile_test_toml_writer.toml";
	{
		std::ofstream file(path, std::ios::binary);
		REQUIRE(file);
		file << "# changed after the writer was built\n[player]\ngain = 1.0\n";
	}

	TomlWriter writer;
	writer.section("player");
	writer.write("gain", 0.25);
	REQUIRE(writer.save(path));

	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	std::ostringstream contents;
	contents << file.rdbuf();
	CHECK(contents.str() ==
	      "# changed after the writer was built\n"
	      "[player]\n"
	      "gain = 0.25\n");
	std::filesystem::remove(path);
}

TEST_CASE("TOML writer inserts missing sections in model order", "[toml][writer]") {
	std::istringstream input(
		"[a]\n"
		"value = 1\n"
		"\n"
		"[c]\n"
		"value = 3\n");
	TomlWriter writer;
	REQUIRE(writer.load(input));

	writer.section("a");
	writer.write("value", 10);
	writer.section("b");
	writer.write("value", 20);
	writer.section("c");
	writer.write("value", 30);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "[a]\n"
	      "value = 10\n"
	      "\n"
	      "[b]\n"
	      "value = 20\n"
	      "\n"
	      "[c]\n"
	      "value = 30\n");
}

TEST_CASE("TOML writer inserts missing values without reordering existing values", "[toml][writer]") {
	SECTION("a missing value is inserted before the next modeled value") {
		std::istringstream input("[values]\nvalA = 1\nvalC = 3\n");
		TomlWriter writer;
		REQUIRE(writer.load(input));

		writer.section("values");
		writer.write("valA", 10);
		writer.write("valB", 20);
		writer.write("valC", 30);

		std::ostringstream output;
		REQUIRE(writer.save(output));
		CHECK(output.str() ==
		      "[values]\n"
		      "valA = 10\n"
		      "valB = 20\n"
		      "valC = 30\n");
	}

	SECTION("manual ordering is preserved and a trailing value stays trailing") {
		std::istringstream input("[values]\nvalB = 2\nvalC = 3\nvalA = 1\n");
		TomlWriter writer;
		REQUIRE(writer.load(input));

		writer.section("values");
		writer.write("valA", 10);
		writer.write("valB", 20);
		writer.write("valC", 30);
		writer.write("valD", 40);

		std::ostringstream output;
		REQUIRE(writer.save(output));
		CHECK(output.str() ==
		      "[values]\n"
		      "valB = 20\n"
		      "valC = 30\n"
		      "valA = 10\n"
		      "valD = 40\n");
	}
}

TEST_CASE("TOML writer keeps comments attached while reconciling", "[toml][writer]") {
	std::istringstream input(
		"# section a\n"
		"[a] # inline section a\n"
		"# value a\n"
		"valA = 1 # inline value a\n"
		"# value c\n"
		"valC = 3 # inline value c\n"
		"\n"
		"# section c line 1\n"
		"# section c line 2\n"
		"[c] # inline section c\n"
		"value = 3 # inline value in c\n");
	TomlWriter writer;
	REQUIRE(writer.load(input));

	writer.section("a");
	writer.write("valA", 10);
	writer.write("valB", 20);
	writer.write("valC", 30);
	writer.section("b");
	writer.write("value", 2);
	writer.section("c");
	writer.write("value", 300);

	std::ostringstream output;
	REQUIRE(writer.save(output));
	CHECK(output.str() ==
	      "# section a\n"
	      "[a] # inline section a\n"
	      "# value a\n"
	      "valA = 10 # inline value a\n"
	      "valB = 20\n"
	      "# value c\n"
	      "valC = 30 # inline value c\n"
	      "\n"
	      "[b]\n"
	      "value = 2\n"
	      "\n"
	      "# section c line 1\n"
	      "# section c line 2\n"
	      "[c] # inline section c\n"
	      "value = 300 # inline value in c\n");
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
