#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

#include "../src/audio/loudness_analyzer.h"

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;

struct ToneSegment {
	uint32_t seconds;
	double peakDbfs;
};

static std::optional<double> try_measure_ebu_tones(
		std::initializer_list<ToneSegment> segments) {
	LoudnessAnalyzer analyzer(kSampleRate, kChannels);
	if(!analyzer.valid()) return std::nullopt;
	std::vector<int16_t> oneSecond(kSampleRate * kChannels);

	for(const auto segment : segments) {
		const auto amplitude = std::numeric_limits<int16_t>::max() *
			std::pow(10.0, segment.peakDbfs / 20.0);
		for(uint32_t frame = 0; frame < kSampleRate; ++frame) {
			const auto sample = static_cast<int16_t>(std::lround(amplitude *
				std::sin(2.0 * std::numbers::pi * 1000.0 * frame / kSampleRate)));
			oneSecond[frame * kChannels] = sample;
			oneSecond[frame * kChannels + 1] = sample;
		}
		for(uint32_t second = 0; second < segment.seconds; ++second) {
			if(!analyzer.add_interleaved(oneSecond)) return std::nullopt;
		}
	}

	return analyzer.integrated_loudness();
}

static double measure_ebu_tones(std::initializer_list<ToneSegment> segments) {
	const auto loudness = try_measure_ebu_tones(segments);
	REQUIRE(loudness.has_value());
	return loudness.value();
}

} // namespace

// Procedural versions of the integrated-loudness minimum-requirement signals
// from EBU Tech 3341. The specification requires an accuracy of +/-0.1 LU.
TEST_CASE("loudness analyzer matches EBU Tech 3341 reference tones", "[audio][loudness]") {
	SECTION("test case 1: -23 dBFS calibration tone") {
		CHECK(measure_ebu_tones({{20, -23.0}}) == Catch::Approx(-23.0).margin(0.1));
	}

	SECTION("test case 2: -33 dBFS calibration tone") {
		CHECK(measure_ebu_tones({{20, -33.0}}) == Catch::Approx(-33.0).margin(0.1));
	}

	SECTION("test case 4: absolute and relative gating") {
		CHECK(measure_ebu_tones({
			{10, -72.0},
			{10, -36.0},
			{60, -23.0},
			{10, -36.0},
			{10, -72.0},
		}) == Catch::Approx(-23.0).margin(0.1));
	}
}

TEST_CASE("loudness analyzer rejects incomplete PCM frames", "[audio][loudness]") {
	LoudnessAnalyzer analyzer(48000, 2);
	REQUIRE(analyzer.valid());
	const std::vector<int16_t> samples = {1, 2, 3};
	CHECK_FALSE(analyzer.add_interleaved(samples));
	CHECK_FALSE(analyzer.error().empty());
}

TEST_CASE("loudness analyzer rejects negative infinity for silence", "[audio][loudness]") {
	LoudnessAnalyzer analyzer(kSampleRate, kChannels);
	REQUIRE(analyzer.valid());
	const std::vector<int16_t> silence(kSampleRate * kChannels, 0);
	REQUIRE(analyzer.add_interleaved(silence));
	CHECK_FALSE(analyzer.integrated_loudness().has_value());
	CHECK(analyzer.error() == "integrated loudness is not finite");
}
