// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Minimal TOML subset: flat key/value pairs grouped into [sections].
// Supported value types: string, integer, float, bool.
// Write order is explicit (caller-controlled); read tolerates any order.

struct TomlValue {
	enum class Type { String, Integer, Float, Bool };
	Type        type;
	std::string str;   // String
	int64_t     i = 0; // Integer
	double      f = 0; // Float
	bool        b = false; // Bool
};

class TomlReader {
public:
	// Parse a TOML file. Returns false on I/O error; syntax errors are skipped.
	bool load(const std::filesystem::path &path);

	std::optional<std::string> get_string (std::string_view section, std::string_view key) const;
	std::optional<int64_t>     get_integer(std::string_view section, std::string_view key) const;
	std::optional<double>      get_float  (std::string_view section, std::string_view key) const;
	std::optional<bool>        get_bool   (std::string_view section, std::string_view key) const;

private:
	// key = "section.key"
	std::unordered_map<std::string, TomlValue> m_values;

	const TomlValue *find(std::string_view section, std::string_view key) const;
};

class TomlWriter {
public:
	// Section/key write methods — output order matches call order.
	void section(std::string_view name);
	void write(std::string_view key, std::string_view   value);
	void write(std::string_view key, int64_t            value);
	void write_hex(std::string_view key, int64_t        value); // integer as 0x…
	void write(std::string_view key, double             value);
	void write(std::string_view key, bool               value);

	// Serialise to file. Returns false on error.
	bool save(const std::filesystem::path &path) const;

private:
	std::string m_buf;
	bool        m_first_section = true;
};
