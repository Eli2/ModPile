// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "task_util.h"

#include <algorithm>
#include <utility>

TaskControl::Scope::Scope(Scope &&other) noexcept
	: m_owner(std::exchange(other.m_owner, nullptr)), m_id(other.m_id)
{
}

TaskControl::Scope& TaskControl::Scope::operator=(Scope &&other) noexcept {
	if(this != &other) {
		release();
		m_owner = std::exchange(other.m_owner, nullptr);
		m_id = other.m_id;
	}
	return *this;
}

TaskControl::Scope::~Scope() {
	release();
}

void TaskControl::Scope::release() {
	if(m_owner) {
		m_owner->pop(m_id);
		m_owner = nullptr;
	}
}

void TaskControl::Scope::label(std::string label) {
	if(m_owner) m_owner->set_label(m_id, std::move(label));
}

void TaskControl::Scope::progress(uint64_t current, uint64_t total, std::string_view unit) {
	if(m_owner) m_owner->set_progress(m_id, TaskProgress{current, total, std::string(unit)});
}

void TaskControl::Scope::counter(uint64_t current, std::string_view unit) {
	if(m_owner) m_owner->set_progress(m_id, TaskProgress{current, std::nullopt, std::string(unit)});
}

void TaskControl::Scope::clear_progress() {
	if(m_owner) m_owner->set_progress(m_id, std::nullopt);
}

TaskControl::Scope TaskControl::Scope::scope(std::string label) const {
	if(!m_owner) return {};
	return m_owner->create_scope(std::move(label), m_id);
}

TaskControl::Scope TaskControl::scope(std::string label) {
	return create_scope(std::move(label), std::nullopt);
}

TaskControl::Scope TaskControl::create_scope(
		std::string label, std::optional<uint64_t> parentId) {
	std::lock_guard lock(m_mutex);
	const auto creator = std::this_thread::get_id();
	uint64_t resolvedParent = 0;
	if(parentId) {
		const auto parent = std::find_if(m_frames.begin(), m_frames.end(),
			[&](const Frame &frame) { return frame.id == *parentId; });
		if(parent == m_frames.end()) return {};
		resolvedParent = *parentId;
	} else {
		const auto parent = std::find_if(m_frames.rbegin(), m_frames.rend(),
			[&](const Frame &frame) { return frame.creator == creator; });
		if(parent != m_frames.rend()) resolvedParent = parent->id;
	}

	const auto id = m_next_id++;
	Frame frame;
	frame.id = id;
	frame.parentId = resolvedParent;
	frame.creator = creator;
	frame.label = std::move(label);
	m_frames.push_back(std::move(frame));
	return Scope(this, id);
}

TaskStatus TaskControl::snapshot() const {
	std::lock_guard lock(m_mutex);
	TaskStatus result;
	result.outcome = m_outcome;
	result.message = m_message;
	const auto append_children = [&](auto &&self, uint64_t parentId,
			std::vector<TaskStatusFrame> &destination) -> void {
		for(const auto &frame : m_frames) {
			if(frame.parentId != parentId) continue;
			TaskStatusFrame statusFrame{frame.label, frame.progress, {}};
			self(self, frame.id, statusFrame.children);
			destination.push_back(std::move(statusFrame));
		}
	};
	append_children(append_children, 0, result.frames);
	return result;
}

void TaskControl::reset() {
	std::lock_guard lock(m_mutex);
	m_frames.clear();
	m_outcome = TaskStatus::Outcome::Running;
	m_message.clear();
}

void TaskControl::succeed(std::string message) {
	finish(TaskStatus::Outcome::Succeeded, std::move(message));
}

void TaskControl::fail(std::string message) {
	finish(TaskStatus::Outcome::Failed, std::move(message));
}

void TaskControl::aborted(std::string message) {
	finish(TaskStatus::Outcome::Aborted, std::move(message));
}

void TaskControl::pop(uint64_t id) {
	std::lock_guard lock(m_mutex);
	std::vector<uint64_t> removed{id};
	for(size_t i = 0; i < removed.size(); ++i) {
		for(const auto &frame : m_frames) {
			if(frame.parentId == removed[i]) removed.push_back(frame.id);
		}
	}
	m_frames.erase(std::remove_if(m_frames.begin(), m_frames.end(), [&](const Frame &frame) {
		return std::find(removed.begin(), removed.end(), frame.id) != removed.end();
	}), m_frames.end());
}

void TaskControl::set_label(uint64_t id, std::string label) {
	std::lock_guard lock(m_mutex);
	const auto it = std::find_if(m_frames.begin(), m_frames.end(), [id](const Frame &frame) {
		return frame.id == id;
	});
	if(it != m_frames.end()) it->label = std::move(label);
}

void TaskControl::set_progress(uint64_t id, std::optional<TaskProgress> progress) {
	std::lock_guard lock(m_mutex);
	const auto it = std::find_if(m_frames.begin(), m_frames.end(), [id](const Frame &frame) {
		return frame.id == id;
	});
	if(it != m_frames.end()) it->progress = std::move(progress);
}

void TaskControl::finish(TaskStatus::Outcome outcome, std::string message) {
	std::lock_guard lock(m_mutex);
	m_outcome = outcome;
	m_message = std::move(message);
}
