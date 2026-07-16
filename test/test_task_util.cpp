#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <latch>
#include <thread>

#include "../src/task_util.h"

TEST_CASE("task status represents nested scopes and structured progress", "[task][status]") {
	TaskControl control;
	auto outer = control.scope("Importing collection");
	outer.progress(2, 5, "archives");

	{
		auto inner = control.scope("songs.zip");
		inner.counter(17, "files");

		const auto status = control.snapshot();
		REQUIRE(status.frames.size() == 1);
		CHECK(status.frames[0].label == "Importing collection");
		REQUIRE(status.frames[0].progress.has_value());
		CHECK(status.frames[0].progress->current == 2);
		CHECK(status.frames[0].progress->total == 5);
		CHECK(status.frames[0].progress->unit == "archives");
		REQUIRE(status.frames[0].children.size() == 1);
		CHECK(status.frames[0].children[0].label == "songs.zip");
		REQUIRE(status.frames[0].children[0].progress.has_value());
		CHECK_FALSE(status.frames[0].children[0].progress->total.has_value());
		CHECK(status.frames[0].children[0].progress->current == 17);
	}

	const auto status = control.snapshot();
	REQUIRE(status.frames.size() == 1);
	CHECK(status.frames[0].label == "Importing collection");
}

TEST_CASE("task status supports concurrent sibling scopes", "[task][status][threading]") {
	TaskControl control;
	auto root = control.scope("Analyzing tracks");
	std::array<std::thread, 4> workers;
	std::latch ready(workers.size());
	std::latch release(1);
	for(size_t i = 0; i < workers.size(); ++i) {
		workers[i] = std::thread([&, i] {
			auto worker = root.scope(std::format("Worker {}", i + 1));
			auto file = worker.scope(std::format("track-{}.mod", i + 1));
			file.counter(i + 10, "seconds decoded");
			ready.count_down();
			release.wait();
		});
	}

	ready.wait();
	const auto status = control.snapshot();
	release.count_down();
	for(auto &worker : workers) worker.join();

	REQUIRE(status.frames.size() == 1);
	REQUIRE(status.frames[0].children.size() == 4);
	for(size_t i = 0; i < workers.size(); ++i) {
		const auto label = std::format("Worker {}", i + 1);
		const auto it = std::find_if(status.frames[0].children.begin(),
			status.frames[0].children.end(), [&](const TaskStatusFrame &worker) {
				return worker.label == label;
			});
		REQUIRE(it != status.frames[0].children.end());
		const auto &worker = *it;
		REQUIRE(worker.children.size() == 1);
		CHECK(worker.children[0].label == std::format("track-{}.mod", i + 1));
		REQUIRE(worker.children[0].progress.has_value());
		CHECK(worker.children[0].progress->current == i + 10);
	}

	CHECK(control.snapshot().frames[0].children.empty());
}

TEST_CASE("task status records typed outcomes", "[task][status]") {
	TaskControl control;
	control.fail("database is busy");

	auto status = control.snapshot();
	CHECK(status.outcome == TaskStatus::Outcome::Failed);
	CHECK(status.message == "database is busy");

	control.reset();
	status = control.snapshot();
	CHECK(status.outcome == TaskStatus::Outcome::Running);
	CHECK(status.message.empty());
}
