/*
Auto Mixer
Copyright (C) 2026 Belloccc

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

// Ausgangsstufe: geglättete Verstärkung und True-Peak-Limiter

#include "signals.hpp"
#include "test-framework.hpp"

#include <dsp/output-stage.hpp>
#include <dsp/true-peak.hpp>

using namespace am::dsp;

namespace {

constexpr double kRate = 48000.0;

struct Result {
	std::vector<float> left;
	std::vector<float> right;
};

// Schickt ein Stereosignal in Stücken durch die Ausgangsstufe; gainAt liefert die Verstärkung je Stück
template<typename GainAt>
Result process(const std::vector<float> &left, const std::vector<float> &right, GainAt gainAt, bool limiter,
	       float ceiling = -1.0f, size_t chunk = 1024)
{
	OutputStage stage;
	OutputStage::Config config;
	config.sampleRate = kRate;
	config.channels = 2;
	stage.configure(config);
	Result out{left, right};
	for (size_t pos = 0; pos < left.size(); pos += chunk) {
		float *data[2] = {out.left.data() + pos, out.right.data() + pos};
		stage.process(data, std::min(chunk, left.size() - pos), gainAt(pos), limiter, ceiling);
	}
	return out;
}

float truePeakDb(const Result &result, size_t skip = 0)
{
	TruePeakMeter meter;
	meter.configure(2, 0.0f);
	const float *data[2] = {result.left.data() + skip, result.right.data() + skip};
	return linearToDb(meter.process(data, result.left.size() - skip));
}

} // namespace

TEST_CASE("output-stage", latency_is_constant)
{
	OutputStage stage;
	OutputStage::Config config;
	config.sampleRate = kRate;
	stage.configure(config);
	CHECK(stage.latency() == 79); // 8 Samples Überabtastung + 72 Samples Lookahead − 1

	// Ein Impuls kommt genau um die Latenz verzögert heraus, mit und ohne Limiter
	for (const bool limiter : {false, true}) {
		std::vector<float> impulse(512, 0.0f);
		impulse[10] = 0.5f;
		const Result out = process(impulse, impulse, [](size_t) { return 0.0f; }, limiter);
		CHECK_NEAR(out.left[10 + 79], 0.5, 1e-6);
		CHECK_NEAR(out.left[10 + 78], 0.0, 1e-6);
	}
}

TEST_CASE("output-stage", applies_gain_smoothly)
{
	const auto tone = signals::sine(kRate, 997.0, 1.0, -20.0);
	// Sprung von 0 auf −12 dB nach 0,5 s
	const Result out = process(tone, tone, [](size_t pos) { return pos < 24000 ? 0.0f : -12.0f; }, false);
	float lastPeak = 0.0f;
	for (size_t i = 46000; i < 48000; i++)
		lastPeak = std::max(lastPeak, std::fabs(out.left[i]));
	CHECK_NEAR(linearToDb(lastPeak), -32.0, 0.1);

	// Keine Knackser: die Verstärkung ändert sich pro Sample nur minimal (Verhältnis Ausgang/Eingang)
	double maxStep = 0.0;
	double previous = 1.0;
	for (size_t i = 24000; i < 30000; i++) {
		const float in = tone[i - 79];
		if (std::fabs(in) < 0.02f)
			continue;
		const double gain = out.left[i] / in;
		maxStep = std::max(maxStep, std::fabs(gain - previous));
		previous = gain;
	}
	CHECK(maxStep < 0.01);
}

TEST_CASE("output-stage", limiter_keeps_true_peak_below_ceiling)
{
	// Lauter Sinus, +12 dB Verstärkung: ohne Limiter weit über 0 dBFS
	const auto tone = signals::sine(kRate, 997.0, 2.0, -3.0);
	const Result out = process(tone, tone, [](size_t) { return 12.0f; }, true);
	CHECK(truePeakDb(out) <= -0.9f);
	// … und nach dem Einschwingen nicht unnötig leise
	CHECK(truePeakDb(out, 48000) >= -1.6f);
}

TEST_CASE("output-stage", limiter_catches_inter_sample_peaks)
{
	// Sample-Spitzen bei −1,5 dBFS, wahre Spitze +1,5 dBTP: ein reiner Sample-Peak-Limiter würde das übersehen
	const auto tone = signals::sine(kRate, 12000.0, 1.0, 1.5, 45.0);
	const Result out = process(tone, tone, [](size_t) { return 0.0f; }, true);
	CHECK(truePeakDb(out) <= -0.9f);
}

TEST_CASE("output-stage", limiter_handles_transients)
{
	// Explosion: aus leisem Rauschen springt es plötzlich um 30 dB nach oben
	auto noise = signals::pinkNoise(kRate, 3.0, 21);
	signals::scaleTo(kRate, noise, -40.0f);
	signals::gainRange(noise, kRate, 1.0, 1.5, 34.0);
	const Result out = process(noise, noise, [](size_t) { return 6.0f; }, true);
	CHECK(truePeakDb(out) <= -0.9f);
}

TEST_CASE("output-stage", without_limiter_only_gain)
{
	const auto tone = signals::sine(kRate, 997.0, 1.0, -3.0);
	const Result out = process(tone, tone, [](size_t) { return 6.0f; }, false);
	// Ohne Limiter darf es übersteuern: die Stufe soll nichts heimlich begrenzen
	CHECK(truePeakDb(out, 24000) > 2.5f);
}

TEST_CASE("output-stage", odd_chunk_sizes)
{
	// OBS liefert meist 1024 Samples, aber nicht immer
	const auto tone = signals::sine(kRate, 997.0, 1.0, -3.0);
	const Result a = process(tone, tone, [](size_t) { return 9.0f; }, true, -1.0f, 1024);
	const Result b = process(tone, tone, [](size_t) { return 9.0f; }, true, -1.0f, 333);
	double diff = 0.0;
	for (size_t i = 0; i < a.left.size(); i++)
		diff = std::max(diff, static_cast<double>(std::fabs(a.left[i] - b.left[i])));
	CHECK(diff < 1e-5);
}
