// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct TaskProgress {
	uint64_t current = 0;
	std::optional<uint64_t> total;
	std::string unit;
};

struct TaskStatusFrame {
	std::string label;
	std::optional<TaskProgress> progress;
	std::vector<TaskStatusFrame> children;
};

struct TaskStatus {
	enum class Outcome { Running, Succeeded, Failed, Aborted };

	std::vector<TaskStatusFrame> frames;
	Outcome outcome = Outcome::Running;
	std::string message;
};

class TaskControl {
public:
	class Scope {
	public:
		Scope() = default;
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		Scope(Scope &&other) noexcept;
		Scope& operator=(Scope &&other) noexcept;
		~Scope();

		void label(std::string label);
		void progress(uint64_t current, uint64_t total, std::string_view unit = {});
		void counter(uint64_t current, std::string_view unit = {});
		void clear_progress();
		Scope scope(std::string label) const;

	private:
		friend class TaskControl;
		Scope(TaskControl *owner, uint64_t id) : m_owner(owner), m_id(id) {}
		void release();

		TaskControl *m_owner = nullptr;
		uint64_t m_id = 0;
	};

	// Public because libcurl requires a stable atomic address for its callback.
	std::atomic_bool abort = false;

	Scope scope(std::string label);
	TaskStatus snapshot() const;
	void reset();
	void succeed(std::string message = {});
	void fail(std::string message);
	void aborted(std::string message = {});

private:
	struct Frame {
		uint64_t id = 0;
		uint64_t parentId = 0;
		std::thread::id creator;
		std::string label;
		std::optional<TaskProgress> progress;
	};

	Scope create_scope(std::string label, std::optional<uint64_t> parentId);
	void pop(uint64_t id);
	void set_label(uint64_t id, std::string label);
	void set_progress(uint64_t id, std::optional<TaskProgress> progress);
	void finish(TaskStatus::Outcome outcome, std::string message);

	mutable std::mutex m_mutex;
	std::vector<Frame> m_frames;
	TaskStatus::Outcome m_outcome = TaskStatus::Outcome::Running;
	std::string m_message;
	uint64_t m_next_id = 1;
};
