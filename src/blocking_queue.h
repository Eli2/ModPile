// From stack overflow
#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <vector>

template <typename T>
class queue {
	std::mutex              d_mutex;
	std::condition_variable d_condition;
	std::deque<T>           d_queue;
public:
	void push(T const& value) {
		{
			std::unique_lock<std::mutex> lock(d_mutex);
			d_queue.push_front(value);
		}
		d_condition.notify_one();
	}
	T pop() {
		std::unique_lock<std::mutex> lock(d_mutex);
		d_condition.wait(lock, [=, this]{
			return !this->d_queue.empty();
		});
		T rc(std::move(d_queue.back()));
		d_queue.pop_back();
		return rc;
	}
	// Returns a snapshot in execution order (index 0 = next to run).
	std::vector<T> snapshot() {
		std::lock_guard<std::mutex> lock(d_mutex);
		return std::vector<T>(d_queue.rbegin(), d_queue.rend());
	}
	// Removes the item at execution-order index (0 = next to run).
	bool remove(size_t index) {
		std::lock_guard<std::mutex> lock(d_mutex);
		if(index >= d_queue.size()) return false;
		d_queue.erase(d_queue.end() - 1 - static_cast<std::ptrdiff_t>(index));
		return true;
	}
	void clear() {
		std::lock_guard<std::mutex> lock(d_mutex);
		d_queue.clear();
	}
};
