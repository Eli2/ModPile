#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_chronometer.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "toml.h"

namespace {

std::string make_wide_document(size_t values) {
	std::string document = "# generated wide-table benchmark\n[values]\n";
	document.reserve(document.size() + values * 40);
	for (size_t i = 0; i < values; ++i) {
		document += "value_";
		document += std::to_string(i);
		document += " = ";
		switch (i % 4) {
			case 0:
				document += std::to_string(i);
				break;
			case 1:
				document += '"';
				document += "track-";
				document += std::to_string(i);
				document += '"';
				break;
			case 2:
				document += "[1, 2, 3, 4]";
				break;
			case 3:
				document += "true";
				break;
		}
		document += '\n';
	}
	return document;
}

std::string make_sectioned_document(size_t sections, size_t values_per_section) {
	std::string document = "# generated section benchmark\n";
	document.reserve(document.size() + sections * values_per_section * 32);
	for (size_t section = 0; section < sections; ++section) {
		document += "\n[section_";
		document += std::to_string(section);
		document += "]\n";
		for (size_t value = 0; value < values_per_section; ++value) {
			document += "value_";
			document += std::to_string(value);
			document += " = ";
			document += std::to_string(section * values_per_section + value);
			document += '\n';
		}
	}
	return document;
}

TomlDocument parse_document(std::string_view source) {
	std::istringstream input{std::string(source)};
	TomlReader reader;
	if (!reader.load(input)) return {};
	return reader.document();
}

size_t parse_once(std::string_view source) {
	std::istringstream input{std::string(source)};
	TomlReader reader;
	if (!reader.load(input)) return 0;
	return reader.document().root.table.size();
}

size_t serialize_once(const TomlDocument &document) {
	TomlWriter writer;
	writer.load(document);
	std::ostringstream output;
	if (!writer.save(output)) return 0;
	return output.view().size();
}

std::vector<std::string> make_keys(size_t count) {
	std::vector<std::string> keys;
	keys.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		keys.push_back("value_" + std::to_string(i));
	}
	return keys;
}

size_t update_once(
	const TomlDocument &document,
	const std::vector<std::string> &keys)
{
	TomlWriter writer;
	writer.load(document);
	writer.section("values");
	for (size_t i = 0; i < keys.size(); ++i) {
		writer.write(keys[i], static_cast<int64_t>(i + 1));
	}
	const auto *values = writer.document().root.find("values");
	return values ? values->table.size() : 0;
}

} // namespace

TEST_CASE("TOML parser scaling", "[!benchmark][toml][parse]") {
	const auto wide100 = make_wide_document(100);
	const auto wide1k = make_wide_document(1'000);
	const auto wide5k = make_wide_document(5'000);
	const auto sections100 = make_sectioned_document(100, 10);
	const auto sections1k = make_sectioned_document(1'000, 10);

	REQUIRE(parse_once(wide100) != 0);
	REQUIRE(parse_once(wide1k) != 0);
	REQUIRE(parse_once(wide5k) != 0);
	REQUIRE(parse_once(sections100) != 0);
	REQUIRE(parse_once(sections1k) != 0);

	BENCHMARK("parse one table with 100 values") {
		return parse_once(wide100);
	};
	BENCHMARK("parse one table with 1,000 values") {
		return parse_once(wide1k);
	};
	BENCHMARK("parse one table with 5,000 values") {
		return parse_once(wide5k);
	};
	BENCHMARK("parse 100 sections with 10 values each") {
		return parse_once(sections100);
	};
	BENCHMARK("parse 1,000 sections with 10 values each") {
		return parse_once(sections1k);
	};
}

TEST_CASE("profile one large TOML parse without benchmark calibration",
	"[!benchmark][toml][cachegrind-parse]") {
	const auto document = make_wide_document(50'000);
	REQUIRE(parse_once(document) != 0);
}

TEST_CASE("TOML ordered document lookup scaling", "[!benchmark][toml][lookup]") {
	const auto document = parse_document(make_wide_document(10'000));
	const auto *values = document.root.find("values");
	REQUIRE(values);
	REQUIRE(values->table.size() == 10'000);

	BENCHMARK("find first of 10,000 values") {
		return values->find("value_0");
	};
	BENCHMARK("find middle of 10,000 values") {
		return values->find("value_5000");
	};
	BENCHMARK("find last of 10,000 values") {
		return values->find("value_9999");
	};
	BENCHMARK("find missing among 10,000 values") {
		return values->find("missing");
	};
}

TEST_CASE("TOML writer scaling", "[!benchmark][toml][write]") {
	const auto document1k = parse_document(make_wide_document(1'000));
	const auto document10k = parse_document(make_wide_document(10'000));
	const auto keys100 = make_keys(100);
	const auto keys1k = make_keys(1'000);

	REQUIRE(serialize_once(document1k) != 0);
	REQUIRE(serialize_once(document10k) != 0);
	REQUIRE(update_once(document1k, keys100) == 1'000);
	REQUIRE(update_once(document1k, keys1k) == 1'000);

	BENCHMARK("serialize one table with 1,000 values") {
		return serialize_once(document1k);
	};
	BENCHMARK("serialize one table with 10,000 values") {
		return serialize_once(document10k);
	};
	BENCHMARK_ADVANCED("update 100 of 1,000 ordered values")(
		Catch::Benchmark::Chronometer meter)
	{
		meter.measure([&] { return update_once(document1k, keys100); });
	};
	BENCHMARK_ADVANCED("update 1,000 of 1,000 ordered values")(
		Catch::Benchmark::Chronometer meter)
	{
		meter.measure([&] { return update_once(document1k, keys1k); });
	};
}
