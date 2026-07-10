// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <array>
#include <cstring>

// Single-producer, single-consumer lock-free ring buffer.
// N must be a power of 2.
template<typename T, size_t N>
class SPSCRingBuffer {
	static_assert((N & (N - 1)) == 0, "N must be a power of 2");
	static constexpr size_t MASK = N - 1;

	alignas(64) std::atomic<size_t> m_head{0}; // written by producer
	alignas(64) std::atomic<size_t> m_tail{0}; // written by consumer
	std::array<T, N> m_data;

public:
	// Non-blocking write. Returns number of items actually written.
	size_t push(const T* items, size_t count) {
		size_t head  = m_head.load(std::memory_order_relaxed);
		size_t tail  = m_tail.load(std::memory_order_acquire);
		size_t avail = N - (head - tail);
		if (count > avail) {
			count = avail;
		}
		
		for (size_t i = 0; i < count; ++i) {
			m_data[(head + i) & MASK] = items[i];
		}

		m_head.store(head + count, std::memory_order_release);
		return count;
	}

	// Non-blocking read. Returns number of items actually read.
	size_t pop(T* items, size_t count) {
		size_t tail  = m_tail.load(std::memory_order_relaxed);
		size_t head  = m_head.load(std::memory_order_acquire);
		size_t avail = head - tail;
		if (count > avail) {
			count = avail;
		}
		
		for (size_t i = 0; i < count; ++i) {
			items[i] = m_data[(tail + i) & MASK];
		}

		m_tail.store(tail + count, std::memory_order_release);
		return count;
	}

	size_t available() const {
		size_t head = m_head.load(std::memory_order_acquire);
		size_t tail = m_tail.load(std::memory_order_acquire);
		return head - tail;
	}
};
