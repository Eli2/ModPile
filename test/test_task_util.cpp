#include <catch2/catch_test_macros.hpp>

#include "../src/task_util.h"

TEST_CASE("task status represents nested scopes and structured progress", "[task][status]") {
	TaskControl control;
	auto outer = control.scope("Importing collection");
	outer.progress(2, 5, "archives");

	{
		auto inner = control.scope("songs.zip");
		inner.counter(17, "files");

		const auto status = control.snapshot();
		REQUIRE(status.frames.size() == 2);
		CHECK(status.frames[0].label == "Importing collection");
		REQUIRE(status.frames[0].progress.has_value());
		CHECK(status.frames[0].progress->current == 2);
		CHECK(status.frames[0].progress->total == 5);
		CHECK(status.frames[0].progress->unit == "archives");
		CHECK(status.frames[1].label == "songs.zip");
		REQUIRE(status.frames[1].progress.has_value());
		CHECK_FALSE(status.frames[1].progress->total.has_value());
		CHECK(status.frames[1].progress->current == 17);
	}

	const auto status = control.snapshot();
	REQUIRE(status.frames.size() == 1);
	CHECK(status.frames[0].label == "Importing collection");
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
