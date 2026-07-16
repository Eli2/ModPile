#include <catch2/catch_test_macros.hpp>

#include <array>

#include "../src/audio/duration_analyzer.h"

TEST_CASE("audible duration ignores trailing digital silence", "[audio][silence]") {
	PcmAudibleDuration duration(1000, 2);
	const std::array<int16_t, 8> samples = {
		0, 0,
		12, 0,
		0, 0,
		0, 0,
	};
	duration.add_interleaved(samples);
	CHECK(duration.milliseconds() == 2);

	const std::array<int16_t, 4> silence = {0, 0, 0, 0};
	duration.add_interleaved(silence);
	CHECK(duration.milliseconds() == 2);
}

TEST_CASE("audible duration handles chunked stereo PCM", "[audio][silence]") {
	PcmAudibleDuration duration(1000, 2);
	const std::array<int16_t, 4> first = {0, 0, 0, 0};
	const std::array<int16_t, 4> second = {0, -1, 0, 0};
	duration.add_interleaved(first);
	duration.add_interleaved(second);
	CHECK(duration.milliseconds() == 3);
}

TEST_CASE("fully silent PCM has zero audible duration", "[audio][silence]") {
	PcmAudibleDuration duration(48000, 2);
	const std::array<int16_t, 8> silence = {};
	duration.add_interleaved(silence);
	CHECK(duration.milliseconds() == 0);
}
