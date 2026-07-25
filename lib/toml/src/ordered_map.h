// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

template<typename Key>
struct OrderedMapHash : std::hash<Key> {};

template<>
struct OrderedMapHash<std::string> {
	using is_transparent = void;

	size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

template<
	typename Key,
	typename T,
	typename Hash = OrderedMapHash<Key>,
	typename KeyEqual = std::equal_to<>>
class OrderedMap {
public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<const Key, T>;
	using size_type = size_t;
	using difference_type = std::ptrdiff_t;
	using reference = value_type &;
	using const_reference = const value_type &;

private:
	using Storage = std::vector<value_type>;
	using Index = std::unordered_map<Key, size_type, Hash, KeyEqual>;

public:
	using iterator = typename Storage::iterator;
	using const_iterator = typename Storage::const_iterator;

	OrderedMap() = default;
	OrderedMap(const OrderedMap &other)
		: m_entries(other.m_entries)
	{
		if (other.m_index) m_index = std::make_unique<Index>(*other.m_index);
	}
	OrderedMap(OrderedMap &&) noexcept = default;

	OrderedMap &operator=(const OrderedMap &other) {
		if (this == &other) return *this;
		OrderedMap copy(other);
		swap(copy);
		return *this;
	}

	OrderedMap &operator=(OrderedMap &&) noexcept = default;

	iterator begin() noexcept { return m_entries.begin(); }
	const_iterator begin() const noexcept { return m_entries.begin(); }
	const_iterator cbegin() const noexcept { return m_entries.cbegin(); }
	iterator end() noexcept { return m_entries.end(); }
	const_iterator end() const noexcept { return m_entries.end(); }
	const_iterator cend() const noexcept { return m_entries.cend(); }

	[[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
	size_type size() const noexcept { return m_entries.size(); }

	void clear() noexcept {
		m_entries.clear();
		m_index.reset();
	}

	void reserve(size_type count) {
		m_entries.reserve(count);
		index().reserve(count);
	}

	template<typename K>
	iterator find(const K &key) {
		if (!m_index) return m_entries.end();
		const auto found = m_index->find(key);
		return found == m_index->end()
			? m_entries.end()
			: m_entries.begin() + static_cast<difference_type>(found->second);
	}

	template<typename K>
	const_iterator find(const K &key) const {
		if (!m_index) return m_entries.end();
		const auto found = m_index->find(key);
		return found == m_index->end()
			? m_entries.end()
			: m_entries.begin() + static_cast<difference_type>(found->second);
	}

	template<typename K>
	bool contains(const K &key) const {
		return m_index && m_index->find(key) != m_index->end();
	}

	template<typename K>
	size_type count(const K &key) const {
		return contains(key) ? 1 : 0;
	}

	template<typename K>
	T &at(const K &key) {
		const auto found = find(key);
		if (found == end()) throw std::out_of_range("OrderedMap::at");
		return found->second;
	}

	template<typename K>
	const T &at(const K &key) const {
		const auto found = find(key);
		if (found == end()) throw std::out_of_range("OrderedMap::at");
		return found->second;
	}

	T &operator[](const Key &key) {
		return try_emplace(key).first->second;
	}

	T &operator[](Key &&key) {
		return try_emplace(std::move(key)).first->second;
	}

	template<typename K, typename... Args>
	std::pair<iterator, bool> try_emplace(K &&key, Args &&...args) {
		if (const auto existing = find(key); existing != end()) {
			return {existing, false};
		}

		const auto position = m_entries.size();
		m_entries.emplace_back(
			std::piecewise_construct,
			std::forward_as_tuple(std::forward<K>(key)),
			std::forward_as_tuple(std::forward<Args>(args)...));
		try {
			index().emplace(m_entries.back().first, position);
		} catch (...) {
			m_entries.pop_back();
			throw;
		}
		return {m_entries.begin() + static_cast<difference_type>(position), true};
	}

	template<typename K, typename... Args>
	std::pair<iterator, bool> emplace(K &&key, Args &&...args) {
		return try_emplace(
			std::forward<K>(key),
			std::forward<Args>(args)...);
	}

	template<typename K, typename M>
	std::pair<iterator, bool> insert_or_assign(K &&key, M &&value) {
		if (const auto existing = find(key); existing != end()) {
			existing->second = std::forward<M>(value);
			return {existing, false};
		}
		return try_emplace(std::forward<K>(key), std::forward<M>(value));
	}

	std::pair<iterator, bool> insert(const value_type &value) {
		return try_emplace(value.first, value.second);
	}

	std::pair<iterator, bool> insert(value_type &&value) {
		return try_emplace(value.first, std::move(value.second));
	}

	iterator erase(iterator position) {
		return erase(const_iterator(position));
	}

	iterator erase(const_iterator position) {
		const auto removed = static_cast<size_type>(position - cbegin());
		if (removed >= size()) return end();

		Storage replacement;
		replacement.reserve(m_entries.size() - 1);
		for (size_type i = 0; i < m_entries.size(); ++i) {
			if (i == removed) continue;
			replacement.emplace_back(m_entries[i].first, std::move(m_entries[i].second));
		}
		m_entries = std::move(replacement);
		rebuild_index();
		return m_entries.begin() +
			static_cast<difference_type>(std::min(removed, m_entries.size()));
	}

	template<typename K>
	size_type erase(const K &key) {
		const auto existing = find(key);
		if (existing == end()) return 0;
		erase(existing);
		return 1;
	}

	void swap(OrderedMap &other) noexcept {
		m_entries.swap(other.m_entries);
		m_index.swap(other.m_index);
	}

	friend void swap(OrderedMap &left, OrderedMap &right) noexcept {
		left.swap(right);
	}

private:
	Index &index() {
		if (!m_index) m_index = std::make_unique<Index>();
		return *m_index;
	}

	void rebuild_index() {
		if (m_entries.empty()) {
			m_index.reset();
			return;
		}
		m_index = std::make_unique<Index>();
		m_index->reserve(m_entries.size());
		for (size_type i = 0; i < m_entries.size(); ++i) {
			m_index->emplace(m_entries[i].first, i);
		}
	}

	Storage m_entries;
	std::unique_ptr<Index> m_index;
};
