// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

// A blocking FIFO queue. A capacity of zero makes the queue unbounded.
// Closing the queue rejects new items and wakes all waiting producers and
// consumers. Unless close(true) is used, consumers may drain queued items.
template<typename T>
class ThreadSafeQueue {
public:
	explicit ThreadSafeQueue(std::size_t capacity = 0) : m_capacity(capacity) {}

	ThreadSafeQueue(const ThreadSafeQueue&) = delete;
	ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

	// Blocks while a bounded queue is full. When supplied, abort is polled while
	// waiting so callers can cancel without needing access to the queue internals.
	bool push(T value, const std::atomic_bool *abort = nullptr) {
		std::unique_lock lock(m_mutex);
		while(!m_closed && m_capacity != 0 && m_items.size() >= m_capacity) {
			if(abort && abort->load()) return false;
			m_notFull.wait_for(lock, std::chrono::milliseconds(20));
		}
		if(m_closed || (abort && abort->load())) return false;
		m_items.push_back(std::move(value));
		lock.unlock();
		m_notEmpty.notify_one();
		return true;
	}

	// Blocks until an item is available or the closed queue has been drained.
	std::optional<T> pop() {
		std::unique_lock lock(m_mutex);
		m_notEmpty.wait(lock, [&] { return m_closed || !m_items.empty(); });
		if(m_items.empty()) return std::nullopt;
		T value = std::move(m_items.front());
		m_items.pop_front();
		lock.unlock();
		m_notFull.notify_one();
		return value;
	}

	std::optional<T> try_pop() {
		std::unique_lock lock(m_mutex);
		if(m_items.empty()) return std::nullopt;
		T value = std::move(m_items.front());
		m_items.pop_front();
		lock.unlock();
		m_notFull.notify_one();
		return value;
	}

	void close(bool discardPending = false) {
		{
			std::lock_guard lock(m_mutex);
			m_closed = true;
			if(discardPending) m_items.clear();
		}
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;
	std::condition_variable m_notFull;
	std::deque<T> m_items;
	std::size_t m_capacity;
	bool m_closed = false;
};
