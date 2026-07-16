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

TaskControl::Scope TaskControl::scope(std::string label) {
	std::lock_guard lock(m_mutex);
	const auto id = m_next_id++;
	Frame frame;
	frame.id = id;
	frame.label = std::move(label);
	m_frames.push_back(std::move(frame));
	return Scope(this, id);
}

TaskStatus TaskControl::snapshot() const {
	std::lock_guard lock(m_mutex);
	TaskStatus result;
	result.outcome = m_outcome;
	result.message = m_message;
	result.frames.reserve(m_frames.size());
	for(const auto &frame : m_frames) {
		result.frames.push_back({frame.label, frame.progress});
	}
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
	const auto it = std::find_if(m_frames.begin(), m_frames.end(), [id](const Frame &frame) {
		return frame.id == id;
	});
	if(it != m_frames.end()) m_frames.erase(it);
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
