// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <tsl/ordered_map.h>

struct TomlComment {
	std::string text; // Text after '#', excluding the line ending.
	size_t offset = 0;
	size_t line = 1;
	size_t column = 1;
	bool trailing = false;
};

enum class TomlValueFormat {
	Plain,
	IntegerHexLower,
	IntegerHexUpper,
	IntegerOctal,
	IntegerBinary,
	FloatScientificLower,
	FloatScientificUpper,
	FloatCompact
};

struct TomlValue;

struct TomlOffsetDateTime {
	std::string value;
};

struct TomlLocalDateTime {
	std::string value;
};

struct TomlLocalDate {
	std::string value;
};

struct TomlLocalTime {
	std::string value;
};

struct TomlStringHash {
	using is_transparent = void;

	size_t operator()(std::string_view value) const noexcept {
		size_t hash = sizeof(size_t) == 8
			? static_cast<size_t>(14695981039346656037ull)
			: static_cast<size_t>(2166136261u);
		const size_t prime = sizeof(size_t) == 8
			? static_cast<size_t>(1099511628211ull)
			: static_cast<size_t>(16777619u);
		for (const unsigned char byte : value) {
			hash ^= byte;
			hash *= prime;
		}
		return hash;
	}
};

using TomlTableStorage = std::vector<std::pair<std::string, TomlValue>>;
using TomlTable = tsl::ordered_map<
	std::string,
	TomlValue,
	TomlStringHash,
	std::equal_to<>,
	std::allocator<std::pair<std::string, TomlValue>>,
	TomlTableStorage>;

using TomlArray = std::vector<TomlValue>;
using TomlValueData = std::variant<
	std::string,
	int64_t,
	double,
	bool,
	TomlOffsetDateTime,
	TomlLocalDateTime,
	TomlLocalDate,
	TomlLocalTime,
	TomlArray,
	TomlTable>;

struct TomlValue {
	enum class Type {
		String,
		Integer,
		Float,
		Bool,
		DateTime,
		DateTimeLocal,
		DateLocal,
		TimeLocal,
		Array,
		Table
	};

	explicit TomlValue(Type value_type = Type::Table);

	[[nodiscard]] Type type() const noexcept {
		return static_cast<Type>(data.index());
	}

	std::string &text();
	const std::string &text() const;
	int64_t &integer() { return std::get<int64_t>(data); }
	const int64_t &integer() const { return std::get<int64_t>(data); }
	double &floating() { return std::get<double>(data); }
	const double &floating() const { return std::get<double>(data); }
	bool &boolean() { return std::get<bool>(data); }
	const bool &boolean() const { return std::get<bool>(data); }
	TomlArray &array() { return std::get<TomlArray>(data); }
	const TomlArray &array() const { return std::get<TomlArray>(data); }
	TomlTable &table() { return std::get<TomlTable>(data); }
	const TomlTable &table() const { return std::get<TomlTable>(data); }

	TomlValueData data;
	TomlValueFormat format = TomlValueFormat::Plain;
	size_t format_width = 0; // Minimum digits for non-decimal integers.
	std::vector<TomlComment> leading_comments;
	std::optional<TomlComment> trailing_comment;
	std::vector<TomlComment> dangling_comments;

	// Parser metadata used to enforce TOML's table-definition rules.
	bool explicit_table = false;
	bool dotted_table = false;
	bool inline_table = false;
	bool array_of_tables = false;

	TomlValue *find(std::string_view key) noexcept;
	const TomlValue *find(std::string_view key) const noexcept;
	TomlValue &insert(std::string key, TomlValue value);
};

static_assert(
	static_cast<size_t>(TomlValue::Type::Table) + 1 ==
	std::variant_size_v<TomlValueData>);

inline TomlValue *TomlValue::find(std::string_view key) noexcept {
	auto &values = table();
	auto value = values.find(key);
	return value == values.end() ? nullptr : &value.value();
}

inline const TomlValue *TomlValue::find(std::string_view key) const noexcept {
	const auto &values = table();
	const auto value = values.find(key);
	return value == values.end() ? nullptr : &value.value();
}

inline TomlValue &TomlValue::insert(std::string key, TomlValue value) {
	return table().try_emplace(std::move(key), std::move(value)).first.value();
}

struct TomlDocument {
	TomlValue root{TomlValue::Type::Table};
	std::vector<TomlComment> trailing_comments;
};

class TomlReader {
public:
	// Parse a complete TOML 1.0 document. Returns false on I/O or syntax error.
	bool load(std::istream &input);
	bool load(const std::filesystem::path &path);

	std::optional<std::string> get_string (std::string_view section, std::string_view key) const;
	std::optional<int64_t>     get_integer(std::string_view section, std::string_view key) const;
	std::optional<double>      get_float  (std::string_view section, std::string_view key) const;
	std::optional<bool>        get_bool   (std::string_view section, std::string_view key) const;

	const TomlDocument &document() const noexcept { return m_document; }

	bool get(std::string &value, std::string_view section, std::string_view key) const;
	bool get(std::filesystem::path &value, std::string_view section, std::string_view key) const;
	bool get(bool &value, std::string_view section, std::string_view key) const;

	template<std::integral T>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	bool get(T &value, std::string_view section, std::string_view key) const {
		auto parsed = get_integer(section, key);
		if(!parsed)
			return false;
		value = static_cast<T>(*parsed);
		return true;
	}

	template<std::floating_point T>
	bool get(T &value, std::string_view section, std::string_view key) const {
		auto parsed = get_float(section, key);
		if(!parsed)
			return false;
		value = static_cast<T>(*parsed);
		return true;
	}

	template<typename T>
	bool get(std::atomic<T> &value, std::string_view section, std::string_view key) const {
		auto parsed = value.load();
		if(!get(parsed, section, key))
			return false;
		value.store(parsed);
		return true;
	}

private:
	TomlDocument m_document;

	const TomlValue *find(std::string_view section, std::string_view key) const;
};

class TomlWriter {
public:
	void load(const TomlDocument &document);

	// Section/key write methods — output order matches call order.
	void section(std::string_view name);
	void write(std::string_view key, std::string_view   value);
	void write(std::string_view key, int64_t            value);
	void write_hex(std::string_view key, int64_t        value); // integer as 0x…
	void write(std::string_view key, double             value);
	void write(std::string_view key, bool               value);

	const TomlDocument &document() const noexcept { return m_document; }

	template<std::integral T>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	void write(std::string_view key, T value) {
		write(key, static_cast<int64_t>(value));
	}

	template<std::integral T>
		requires (!std::same_as<std::remove_cv_t<T>, bool>)
	void write_hex(std::string_view key, T value) {
		write_hex(key, static_cast<int64_t>(value));
	}

	template<std::floating_point T>
	void write(std::string_view key, T value) {
		write(key, static_cast<double>(value));
	}

	// Serialise to a stream or file. Returns false on error.
	bool save(std::ostream &output) const;
	bool save(const std::filesystem::path &path) const;

private:
	struct SectionWriteOrder {
		std::string name;
		std::vector<std::string> seen_keys;
		std::vector<std::string> pending_keys;
	};

	TomlDocument         m_document;
	std::optional<std::string> m_current_section;
	std::vector<std::string> m_seen_sections;
	std::vector<std::string> m_pending_sections;
	std::vector<SectionWriteOrder> m_section_write_order;

	std::string render() const;
	void write_value(std::string_view key, TomlValue value);
	SectionWriteOrder &write_order_for(std::string_view section);
};
