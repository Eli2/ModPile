// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

	explicit TomlValue(Type value_type = Type::Table) : type(value_type) {}

	Type        type;
	std::string str;   // String and date/time values
	int64_t     i = 0; // Integer
	double      f = 0; // Float
	bool        b = false; // Bool
	std::vector<TomlValue> array;
	std::vector<std::pair<std::string, TomlValue>> table;
	std::string lexical; // Optional preferred spelling used by the writer.

	// Parser metadata used to enforce TOML's table-definition rules.
	bool explicit_table = false;
	bool dotted_table = false;
	bool inline_table = false;
	bool array_of_tables = false;

	TomlValue *find(std::string_view key) noexcept;
	const TomlValue *find(std::string_view key) const noexcept;
	TomlValue &insert(std::string key, TomlValue value);
};

inline TomlValue *TomlValue::find(std::string_view key) noexcept {
	for (auto &[candidate, value] : table) {
		if (candidate == key) return &value;
	}
	return nullptr;
}

inline const TomlValue *TomlValue::find(std::string_view key) const noexcept {
	for (const auto &[candidate, value] : table) {
		if (candidate == key) return &value;
	}
	return nullptr;
}

inline TomlValue &TomlValue::insert(std::string key, TomlValue value) {
	table.emplace_back(std::move(key), std::move(value));
	return table.back().second;
}

struct TomlComment {
	std::string text; // Text after '#', excluding the line ending.
	size_t offset = 0;
	size_t line = 1;
	size_t column = 1;
	bool trailing = false;
};

struct TomlDocument {
	TomlValue root{TomlValue::Type::Table};
	std::vector<TomlComment> comments;
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
	// Load an existing document so writes preserve its layout and unknown content.
	bool load(std::istream &input);
	bool load(const std::filesystem::path &path);

	// Section/key write methods — output order matches call order.
	void section(std::string_view name);
	void write(std::string_view key, std::string_view   value);
	void write(std::string_view key, int64_t            value);
	void write_hex(std::string_view key, int64_t        value); // integer as 0x…
	void write(std::string_view key, double             value);
	void write(std::string_view key, bool               value);

	void set_document(TomlDocument document);
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
	std::string          m_source_document;
	TomlDocument         m_document;
	std::optional<std::string> m_current_section;

	std::string render(std::string document) const;
	void write_value(std::string_view key, TomlValue value);
};
