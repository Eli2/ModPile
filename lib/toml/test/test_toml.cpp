#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>

#include "toml.h"

namespace {

TomlDocument read_toml(std::string_view text) {
	auto [document, error] = toml::from_string(text);
	INFO(error);
	REQUIRE(document);
	return std::move(*document);
}

void check_round_trip(std::string_view text) {
	auto document = read_toml(text);
	const auto serialized = toml::to_string(document);
	CHECK(serialized == text);
	auto reparsed = read_toml(serialized);
	CHECK(toml::to_string(reparsed) == text);
}

} // namespace

TEST_CASE("TOML reader reads every supported scalar type", "[toml][reader]") {
	auto document = read_toml(R"(
		[values]
		text = "hello"
		integer = -42
		hex_lower = 0x2a
		hex_upper = 0xCAFE
		octal = 0o52
		binary = 0b101010
		fraction = -12.5
		exponent = 5e+2
		enabled = true
		disabled = false
	)");
	TomlReader reader(document);

	CHECK(reader.get_string("values", "text") == "hello");
	CHECK(reader.get_integer("values", "integer") == -42);
	CHECK(reader.get_integer("values", "hex_lower") == 42);
	CHECK(reader.get_integer("values", "hex_upper") == 0xCAFE);
	CHECK(reader.get_integer("values", "octal") == 42);
	CHECK(reader.get_integer("values", "binary") == 42);
	CHECK(reader.get_float("values", "fraction") == -12.5);
	CHECK(reader.get_float("values", "exponent") == 500.0);
	CHECK(reader.get_bool("values", "enabled") == true);
	CHECK(reader.get_bool("values", "disabled") == false);

	const auto *values = reader.document().root.find("values");
	REQUIRE(values);
	REQUIRE(values->find("integer"));
	REQUIRE(values->find("hex_lower"));
	REQUIRE(values->find("hex_upper"));
	REQUIRE(values->find("octal"));
	REQUIRE(values->find("binary"));
	REQUIRE(values->find("fraction"));
	REQUIRE(values->find("exponent"));
	CHECK(values->find("integer")->format == TomlValueFormat::Plain);
	CHECK(values->find("hex_lower")->format == TomlValueFormat::IntegerHexLower);
	CHECK(values->find("hex_upper")->format == TomlValueFormat::IntegerHexUpper);
	CHECK(values->find("octal")->format == TomlValueFormat::IntegerOctal);
	CHECK(values->find("binary")->format == TomlValueFormat::IntegerBinary);
	CHECK(values->find("fraction")->format == TomlValueFormat::Plain);
	CHECK(values->find("exponent")->format == TomlValueFormat::FloatScientificLower);
}

TEST_CASE("TOML reader accepts integers where callers request floats", "[toml][reader]") {
	auto document = read_toml("[player]\ngain = 2\n");
	TomlReader reader(document);

	CHECK(reader.get_float("player", "gain") == 2.0);
	CHECK(reader.get_integer("player", "gain") == 2);
}

TEST_CASE("TOML reader assigns values to application types", "[toml][reader]") {
	auto document = read_toml(R"(
		[values]
		path = "/tmp/music"
		integer = -42
		fraction = 1.25
		enabled = true
	)");
	TomlReader reader(document);

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
	auto document = read_toml(R"(
		[values]
		text = "1"
		integer = 1
		float = 1.5
		boolean = true
	)");
	TomlReader reader(document);

	CHECK_FALSE(reader.get_integer("values", "text"));
	CHECK_FALSE(reader.get_bool("values", "integer"));
	CHECK_FALSE(reader.get_integer("values", "float"));
	CHECK_FALSE(reader.get_string("values", "boolean"));
}

TEST_CASE("TOML reader handles whitespace CRLF and comments", "[toml][reader]") {
	auto document = read_toml(
		"# full-line comment\r\n"
		"  [ spaced ]  # section comment\r\n"
		"  key \t=\t \"value # not a comment\"  # value comment\r\n"
		"number = 7 # numeric comment\r\n"
		"flag = true # boolean comment\r\n");
	TomlReader reader(document);

	CHECK(reader.get_string("spaced", "key") == "value # not a comment");
	CHECK(reader.get_integer("spaced", "number") == 7);
	CHECK(reader.get_bool("spaced", "flag") == true);
}

TEST_CASE("TOML reader preserves value section and comment order", "[toml][reader]") {
	auto parsed_document = read_toml(
		"# document comment\n"
		"root = 1 # trailing root comment\n"
		"[z]\n"
		"first = true\n"
		"second = false\n"
		"# section comment\n"
		"[a]\n"
		"value = \"last\"\n");
	TomlReader reader(parsed_document);

	const auto &document = reader.document();
	REQUIRE(document.root.table().size() == 3);
	const auto root_entry = document.root.table().begin();
	const auto z_entry = std::next(root_entry);
	const auto a_entry = std::next(z_entry);
	CHECK(root_entry->first == "root");
	CHECK(z_entry->first == "z");
	CHECK(a_entry->first == "a");
	REQUIRE(z_entry->second.table().size() == 2);
	CHECK(z_entry->second.table().begin()->first == "first");
	CHECK(std::next(z_entry->second.table().begin())->first == "second");

	const auto &root = root_entry->second;
	REQUIRE(root.leading_comments.size() == 1);
	CHECK(root.leading_comments[0].text == " document comment");
	REQUIRE(root.trailing_comment);
	CHECK(root.trailing_comment->text == " trailing root comment");
	const auto &section_a = a_entry->second;
	REQUIRE(section_a.leading_comments.size() == 1);
	CHECK(section_a.leading_comments[0].text == " section comment");
}

TEST_CASE("TOML reader decodes supported basic-string escapes", "[toml][reader]") {
	auto document = read_toml(
		R"([strings]
value = "quote: \" slash: \\ newline:\n tab:\t carriage:\r backspace:\b formfeed:\f"
)");
	TomlReader reader(document);

	CHECK(reader.get_string("strings", "value") ==
	      "quote: \" slash: \\ newline:\n tab:\t carriage:\r backspace:\b formfeed:\f");
}

TEST_CASE("TOML reader preserves UTF-8 strings", "[toml][reader]") {
	auto document = read_toml("[strings]\nvalue = \"Grüße 日本語 🎛️\"\n");
	TomlReader reader(document);
	CHECK(reader.get_string("strings", "value") == "Grüße 日本語 🎛️");
}

TEST_CASE("TOML reader handles signed values exponents and negative zero", "[toml][reader]") {
	auto document = read_toml(R"(
		[numbers]
		positive = +17
		negative = -17
		fraction = +1.25
		lower_exponent = 1e3
		upper_exponent = -2E-2
		negative_zero = -0.0
	)");
	TomlReader reader(document);

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
	auto [document, error] = toml::from_string(R"(
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
	CHECK_FALSE(document);
	CHECK_FALSE(error.empty());
}

TEST_CASE("TOML parser reports parse errors", "[toml][reader]") {
	const auto [invalid_document, invalid_error] =
		toml::from_string("[section]\nvalue = [1,, 2]\n");
	CHECK_FALSE(invalid_document);
	CHECK(invalid_error == "TOML parse error at line 2, column 12.");

	const auto [valid_document, valid_error] =
		toml::from_string("[section]\nvalue = [1, 2]\n");
	REQUIRE(valid_document);
	CHECK(valid_error.empty());
}

TEST_CASE("TOML parser reads files", "[toml][reader]") {
	const auto path =
		std::filesystem::temp_directory_path() / "modpile_test_toml_reader.toml";
	{
		std::ofstream output(path, std::ios::binary);
		REQUIRE(output);
		output << "[section]\nvalue = 42\n";
	}

	auto [document, error] = toml::from_file(path);
	std::filesystem::remove(path);

	INFO(error);
	REQUIRE(document);
	TomlReader reader(*document);
	CHECK(reader.get_integer("section", "value") == 42);
}

TEST_CASE("TOML reader separates sections and missing values", "[toml][reader]") {
	auto document = read_toml("[first]\nvalue = 1\n[second]\nvalue = 2\n");
	TomlReader reader(document);

	CHECK(reader.get_integer("first", "value") == 1);
	CHECK(reader.get_integer("second", "value") == 2);
	CHECK_FALSE(reader.get_integer("third", "value"));
	CHECK_FALSE(reader.get_integer("first", "missing"));
}

TEST_CASE("TOML reader views its supplied document", "[toml][reader]") {
	auto document = read_toml("[section]\nvalue = 2\n");
	TomlReader reader(document);

	CHECK(&reader.document() == &document);
	CHECK(reader.get_integer("section", "value") == 2);
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
	REQUIRE(toml::to_stream(writer.document(), output));
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

TEST_CASE("TOML writer can update a referenced document", "[toml][writer]") {
	TomlDocument document;
	TomlWriter writer(document);
	writer.section("settings");
	writer.write("enabled", true);

	CHECK(&writer.document() == &document);
	REQUIRE(document.root.find("settings"));
	CHECK(document.root.find("settings")->find("enabled")->boolean());
	CHECK(toml::to_string(document) ==
	      "[settings]\n"
	      "enabled = true\n");
}

TEST_CASE("TOML writer serializes structured numeric formats", "[toml][writer]") {
	auto document = read_toml(
		"[numbers]\n"
		"decimal = 42\n"
		"hex_lower = 0x002a\n"
		"hex_upper = 0xCAFE\n"
		"octal = 0o52\n"
		"binary = 0b101010\n"
		"float_plain = 12.5\n"
		"float_lower = 5e+2\n"
		"float_upper = -2E-2\n");
	TomlWriter writer(document);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
	CHECK(output.str() ==
	      "[numbers]\n"
	      "decimal = 42\n"
	      "hex_lower = 0x002a\n"
	      "hex_upper = 0xCAFE\n"
	      "octal = 0o52\n"
	      "binary = 0b101010\n"
	      "float_plain = 12.5\n"
	      "float_lower = 5e+02\n"
	      "float_upper = -2E-02\n");
}

TEST_CASE("TOML table formats preserve their spelling", "[toml][writer][table]") {
	SECTION("standard and nested tables") {
		check_round_trip(
			"[database]\n"
			"server = \"db.example.com\"\n"
			"\n"
			"[database.connection]\n"
			"enabled = true\n"
			"\n"
			"[database.connection.tls]\n"
			"version = \"1.3\"\n");
	}

	SECTION("dotted keys remain dotted keys") {
		check_round_trip(
			"fruit.name = \"apple\"\n"
			"fruit.physical.color = \"red\"\n"
			"fruit.physical.shape = \"round\"\n");
	}

	SECTION("inline tables remain inline") {
		check_round_trip(
			"point = {x = 1, y = 2, metadata = {label = \"origin\"}}\n");
	}

	SECTION("dotted keys within inline tables remain dotted") {
		check_round_trip(
			"point = {position.x = 1, position.y = 2}\n");
	}

	SECTION("arrays of inline tables remain value arrays") {
		check_round_trip(
			"points = [{x = 1, y = 2}, {x = 3, y = 4}]\n");
	}

	SECTION("explicit empty tables are preserved") {
		check_round_trip(
			"[empty]\n"
			"\n"
			"[nested.empty]\n");
	}

	SECTION("arrays of tables") {
		check_round_trip(
			"[[products]]\n"
			"name = \"Hammer\"\n"
			"sku = 738594937\n"
			"\n"
			"[[products]]\n"
			"name = \"Nail\"\n"
			"sku = 284758393\n");
	}

	SECTION("nested tables and arrays within table-array elements") {
		check_round_trip(
			"[[fruits]]\n"
			"name = \"apple\"\n"
			"\n"
			"[fruits.physical]\n"
			"color = \"red\"\n"
			"\n"
			"[[fruits.varieties]]\n"
			"name = \"red delicious\"\n"
			"\n"
			"[[fruits.varieties]]\n"
			"name = \"granny smith\"\n"
			"\n"
			"[[fruits]]\n"
			"name = \"banana\"\n"
			"\n"
			"[[fruits.varieties]]\n"
			"name = \"plantain\"\n");
	}

	SECTION("quoted table path components") {
		check_round_trip(
			"[\"fruit.with.dot\".\"physical color\"]\n"
			"shape = \"round\"\n");
	}
}

TEST_CASE("TOML value formats encode table and array syntax states", "[toml][format]") {
	auto document = read_toml(
		"[explicit]\n"
		"dotted.value = 1\n"
		"inline = {nested.value = 2}\n"
		"plain_array = [1, 2]\n"
		"trailing_array = [1, 2,]\n"
		"\n"
		"[[items]]\n"
		"name = \"first\"\n"
		"\n"
		"[implicit.child]\n");

	CHECK(document.root.format == TomlValueFormat::TableExplicit);
	const auto *explicit_table = document.root.find("explicit");
	REQUIRE(explicit_table);
	CHECK(explicit_table->format == TomlValueFormat::TableExplicit);

	const auto *dotted_table = explicit_table->find("dotted");
	REQUIRE(dotted_table);
	CHECK(dotted_table->format == TomlValueFormat::TableDotted);

	const auto *inline_table = explicit_table->find("inline");
	REQUIRE(inline_table);
	CHECK(inline_table->format == TomlValueFormat::TableInline);
	const auto *inline_dotted_table = inline_table->find("nested");
	REQUIRE(inline_dotted_table);
	CHECK(inline_dotted_table->format == TomlValueFormat::TableInlineDotted);

	REQUIRE(explicit_table->find("plain_array"));
	CHECK(explicit_table->find("plain_array")->format == TomlValueFormat::Plain);
	REQUIRE(explicit_table->find("trailing_array"));
	CHECK(
		explicit_table->find("trailing_array")->format ==
		TomlValueFormat::ArrayTrailingComma);

	const auto *items = document.root.find("items");
	REQUIRE(items);
	CHECK(items->format == TomlValueFormat::ArrayOfTables);
	REQUIRE(items->array().size() == 1);
	CHECK(items->array().front().format == TomlValueFormat::TableExplicit);

	const auto *implicit_table = document.root.find("implicit");
	REQUIRE(implicit_table);
	CHECK(implicit_table->format == TomlValueFormat::TableImplicit);
	REQUIRE(implicit_table->find("child"));
	CHECK(
		implicit_table->find("child")->format ==
		TomlValueFormat::TableExplicit);
}

TEST_CASE("TOML serializer writes programmatic empty tables", "[toml][format]") {
	TomlDocument document;
	document.root.insert("empty", TomlValue{TomlTable{}});

	TomlValue outer{TomlTable{}};
	outer.insert("inner", TomlValue{TomlTable{}});
	document.root.insert("outer", std::move(outer));

	CHECK(toml::to_string(document) ==
	      "[empty]\n"
	      "\n"
	      "[outer]\n"
	      "\n"
	      "[outer.inner]\n");
}

TEST_CASE("TOML comments preserve their attachment and spelling", "[toml][writer][comment]") {
	SECTION("document and root values") {
		check_round_trip(
			"# document comment\n"
			"# before root value\n"
			"root = 1 # after root value\n"
			"# before second root value\n"
			"enabled = true # after second root value\n"
			"# trailing document comment one\n"
			"# trailing document comment two\n");
	}

	SECTION("standard and nested tables") {
		check_round_trip(
			"# before table\n"
			"[table] # after table\n"
			"# before table value\n"
			"value = 1 # after table value\n"
			"\n"
			"# before nested table\n"
			"[table.nested] # after nested table\n"
			"# before nested value\n"
			"enabled = true # after nested value\n");
	}

	SECTION("dotted keys") {
		check_round_trip(
			"# before dotted name\n"
			"fruit.name = \"apple\" # after dotted name\n"
			"# before dotted color\n"
			"fruit.physical.color = \"red\" # after dotted color\n"
			"# before dotted shape\n"
			"fruit.physical.shape = \"round\" # after dotted shape\n");
	}

	SECTION("inline tables") {
		check_round_trip(
			"# before inline table\n"
			"point = {x = 1, y = 2, metadata = {label = \"origin\"}}"
			" # after inline table\n");
	}

	SECTION("quoted explicit empty tables") {
		check_round_trip(
			"# before empty table\n"
			"[\"implicit.parent\".\"empty table\"] # after empty table\n");
	}

	SECTION("arrays of tables") {
		check_round_trip(
			"# before first product\n"
			"[[products]] # after first product\n"
			"# before first name\n"
			"name = \"Hammer\" # after first name\n"
			"\n"
			"# before second product\n"
			"[[products]] # after second product\n"
			"# before second name\n"
			"name = \"Nail\" # after second name\n");
	}

	SECTION("nested tables within table-array elements") {
		check_round_trip(
			"# before fruit\n"
			"[[fruits]] # after fruit\n"
			"name = \"apple\" # after fruit name\n"
			"\n"
			"# before physical table\n"
			"[fruits.physical] # after physical table\n"
			"color = \"red\" # after color\n"
			"\n"
			"# before variety\n"
			"[[fruits.varieties]] # after variety\n"
			"name = \"red delicious\" # after variety name\n");
	}
}

TEST_CASE("TOML multiline array comments preserve their attachment", "[toml][writer][comment][array]") {
	SECTION("comments before and behind every scalar value") {
		check_round_trip(
			"# before array\n"
			"values = [\n"
			"  # before one\n"
			"  1, # after one\n"
			"  # before two\n"
			"  \"two\", # after two\n"
			"  # before three\n"
			"  true, # after three\n"
			"  # before closing bracket\n"
			"] # after array\n");
	}

	SECTION("comments behind compound values") {
		check_round_trip(
			"values = [\n"
			"  {name = \"one\"}, # after inline table\n"
			"  [1, 2], # after nested array\n"
			"  {name = \"three\"}, # after second inline table\n"
			"] # after compound array\n");
	}

	SECTION("comments in an empty multiline array") {
		check_round_trip(
			"empty = [\n"
			"  # inside empty array\n"
			"] # after empty array\n");
	}

	SECTION("compact array trailing comma") {
		check_round_trip("values = [1, 2, 3,]\n");
	}
}

TEST_CASE("TOML writer canonically serializes an ordered document tree", "[toml][writer]") {
	TomlDocument document;

	TomlValue title{std::string{}};
	title.text() = "demo";
	title.leading_comments.push_back({" generated document"});
	document.root.insert("title", std::move(title));

	TomlValue values{TomlTable{}};
	TomlValue enabled{false};
	enabled.boolean() = true;
	values.insert("enabled", std::move(enabled));
	TomlValue numbers{TomlArray{}};
	for (const int number : {1, 2}) {
		TomlValue element{int64_t{}};
		element.integer() = number;
		numbers.array().push_back(std::move(element));
	}
	values.insert("numbers", std::move(numbers));

	TomlValue nested{TomlTable{}};
	TomlValue date{TomlLocalDate{}};
	date.text() = "2026-07-25";
	nested.insert("date", std::move(date));
	values.insert("nested", std::move(nested));
	document.root.insert("values", std::move(values));

	TomlWriter writer(document);
	REQUIRE(writer.document().root.table().begin()->first == "title");
	REQUIRE(std::next(writer.document().root.table().begin())->first == "values");

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
	CHECK(output.str() ==
	      "# generated document\n"
	      "title = \"demo\"\n"
	      "\n"
	      "[values]\n"
	      "enabled = true\n"
	      "numbers = [1, 2]\n"
	      "\n"
	      "[values.nested]\n"
	      "date = 2026-07-25\n");

	auto parsed_document = read_toml(output.str());
	TomlReader reader(parsed_document);
	CHECK(reader.get_string("", "title") == "demo");
	CHECK(reader.get_bool("values", "enabled") == true);
}

TEST_CASE("TOML writer escapes basic strings and round-trips them", "[toml][writer]") {
	const std::string expected = "quote \" slash \\ newline\n tab\t carriage\r";
	TomlWriter writer;
	writer.section("strings");
	writer.write("value", expected);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
	CHECK(output.str() == "[strings]\nvalue = \"quote \\\" slash \\\\ newline\\n tab\\t carriage\\r\"\n");
	auto document = read_toml(output.str());
	TomlReader reader(document);
	CHECK(reader.get_string("strings", "value") == expected);
}

TEST_CASE("TOML writer preserves parsed comments and unknown values", "[toml][writer]") {
	const std::string original =
		"# Personal settings; keep this comment\n"
		"[player] # playback controls\n"
		"gain   = 1.0    # adjusted by the volume knob\n"
		"theme = \"midnight\" # setting unknown to ModPile\n"
		"\n"
		"[library]\n"
		"paths = [\"~/Music\", \"/mnt/modules\"]\n";
	auto document = read_toml(original);
	TomlWriter writer(document);

	writer.section("player");
	writer.write("gain", 0.75);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
	CHECK(output.str() ==
	      "# Personal settings; keep this comment\n"
	      "[player] # playback controls\n"
	      "gain = 0.75 # adjusted by the volume knob\n"
	      "theme = \"midnight\" # setting unknown to ModPile\n"
	      "\n"
	      "[library]\n"
	      "paths = [\"~/Music\", \"/mnt/modules\"]\n");
}

TEST_CASE("TOML writer canonicalizes newlines and appends missing keys", "[toml][writer]") {
	auto document = read_toml("[player]\r\ngain = 1\r\n\r\n[other]\r\nvalue = true\r\n");
	TomlWriter writer(document);

	writer.section("player");
	writer.write("stereo_width", 0.5);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
	CHECK(output.str() ==
	      "[player]\n"
	      "gain = 1\n"
	      "stereo_width = 0.5\n"
	      "\n"
	      "[other]\n"
	      "value = true\n");
}

TEST_CASE("TOML writer updates its loaded document deterministically", "[toml][writer]") {
	auto document = read_toml("[player]\ngain = 1.0\n");
	TomlWriter writer(document);

	writer.section("player");
	writer.write("gain", 0.75);
	writer.write("gain", 0.5);

	std::ostringstream first;
	std::ostringstream second;
	REQUIRE(toml::to_stream(writer.document(), first));
	REQUIRE(toml::to_stream(writer.document(), second));
	CHECK(first.str() == "[player]\ngain = 0.5\n");
	CHECK(second.str() == first.str());
}

TEST_CASE("TOML writer replaces a file from its supplied document", "[toml][writer]") {
	const auto path = std::filesystem::temp_directory_path() / "modpile_test_toml_writer.toml";
	{
		std::ofstream file(path, std::ios::binary);
		REQUIRE(file);
		file << "# changed after the writer was built\n[player]\ngain = 1.0\n";
	}

	TomlWriter writer;
	writer.section("player");
	writer.write("gain", 0.25);
	REQUIRE(toml::to_file(writer.document(), path));

	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	std::ostringstream contents;
	contents << file.rdbuf();
	CHECK(contents.str() ==
	      "[player]\n"
	      "gain = 0.25\n");
	std::filesystem::remove(path);
}

TEST_CASE("TOML writer inserts missing sections in model order", "[toml][writer]") {
	auto document = read_toml(
		"[a]\n"
		"value = 1\n"
		"\n"
		"[c]\n"
		"value = 3\n");
	TomlWriter writer(document);

	writer.section("a");
	writer.write("value", 10);
	writer.section("b");
	writer.write("value", 20);
	writer.section("c");
	writer.write("value", 30);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
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
		auto document = read_toml("[values]\nvalA = 1\nvalC = 3\n");
		TomlWriter writer(document);

		writer.section("values");
		writer.write("valA", 10);
		writer.write("valB", 20);
		writer.write("valC", 30);

		std::ostringstream output;
		REQUIRE(toml::to_stream(writer.document(), output));
		CHECK(output.str() ==
		      "[values]\n"
		      "valA = 10\n"
		      "valB = 20\n"
		      "valC = 30\n");
	}

	SECTION("manual ordering is preserved and a trailing value stays trailing") {
		auto document = read_toml("[values]\nvalB = 2\nvalC = 3\nvalA = 1\n");
		TomlWriter writer(document);

		writer.section("values");
		writer.write("valA", 10);
		writer.write("valB", 20);
		writer.write("valC", 30);
		writer.write("valD", 40);

		std::ostringstream output;
		REQUIRE(toml::to_stream(writer.document(), output));
		CHECK(output.str() ==
		      "[values]\n"
		      "valB = 20\n"
		      "valC = 30\n"
		      "valA = 10\n"
		      "valD = 40\n");
	}
}

TEST_CASE("TOML writer keeps structured comments attached while updating", "[toml][writer]") {
	auto document = read_toml(
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
	TomlWriter writer(document);

	writer.section("a");
	writer.write("valA", 10);
	writer.write("valB", 20);
	writer.write("valC", 30);
	writer.section("b");
	writer.write("value", 2);
	writer.section("c");
	writer.write("value", 300);

	std::ostringstream output;
	REQUIRE(toml::to_stream(writer.document(), output));
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
	const auto [document, error] = toml::from_stream(input);
	CHECK_FALSE(document);
	CHECK(error == "Could not read TOML input.");

	TomlWriter writer;
	writer.section("section");
	std::ostringstream output;
	output.setstate(std::ios::badbit);
	CHECK_FALSE(toml::to_stream(writer.document(), output));
}
