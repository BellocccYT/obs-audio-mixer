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

// Lautheitsmessung gegen die synthetischen Testfälle aus EBU Tech 3341 (Lautheit) und Tech 3342 (Loudness
// Range). Die Testsignale werden hier nachgebaut statt als WAV-Dateien mitgeliefert.

#include "signals.hpp"
#include "test-framework.hpp"

#include <dsp/loudness-meter.hpp>

#include <algorithm>
#include <functional>

using namespace am::dsp;

namespace {

constexpr double kRate = 48000.0;
constexpr double kTolerance = 0.1; // Tech 3341: ±0,1 LU

// Misst ein Signal in Stücken von 1024 Samples (wie OBS sie liefert); onBlock sieht jeden 100-ms-Block
void measure(LoudnessMeter &meter, const std::vector<std::vector<float>> &channels,
	     const std::function<void(const LoudnessMeter &)> &onBlock = {})
{
	const size_t length = channels.front().size();
	std::vector<const float *> data(channels.size());
	uint64_t seen = meter.blocks();
	for (size_t pos = 0; pos < length; pos += 1024) {
		const size_t n = std::min<size_t>(1024, length - pos);
		for (size_t c = 0; c < channels.size(); c++)
			data[c] = channels[c].data() + pos;
		meter.process(data.data(), n);
		if (onBlock && meter.blocks() != seen)
			onBlock(meter);
		seen = meter.blocks();
	}
}

LoudnessMeter stereoMeter(double rate = kRate)
{
	LoudnessMeter meter;
	meter.configure(rate, channelWeights(2));
	return meter;
}

float integratedStereo(const std::vector<float> &mono, double rate = kRate)
{
	LoudnessMeter meter = stereoMeter(rate);
	measure(meter, {mono, mono});
	return meter.integrated();
}

float loudnessRange(const std::vector<float> &mono)
{
	LoudnessMeter meter = stereoMeter();
	measure(meter, {mono, mono});
	return meter.loudnessRange();
}

} // namespace

TEST_CASE("loudness", k_weighting_coefficients_48k)
{
	// Koeffizienten aus ITU-R BS.1770-4, Tabellen 1 und 2
	const KWeighting k = kWeighting(48000.0);
	CHECK_NEAR(k.shelf.b0, 1.53512485958697, 1e-6);
	CHECK_NEAR(k.shelf.b1, -2.69169618940638, 1e-6);
	CHECK_NEAR(k.shelf.b2, 1.19839281085285, 1e-6);
	CHECK_NEAR(k.shelf.a1, -1.69065929318241, 1e-6);
	CHECK_NEAR(k.shelf.a2, 0.73248077421585, 1e-6);
	CHECK_NEAR(k.highpass.a1, -1.99004745483398, 1e-6);
	CHECK_NEAR(k.highpass.a2, 0.99007225036621, 1e-6);
}

TEST_CASE("loudness", tech3341_case1_sine_minus23)
{
	// 997 Hz, −23 dBFS auf beiden Kanälen → M, S und I = −23,0 LUFS
	const std::vector<float> tone = signals::sine(kRate, 997.0, 20.0, -23.0);
	LoudnessMeter meter = stereoMeter();
	measure(meter, {tone, tone});
	CHECK_NEAR(meter.momentary(), -23.0, kTolerance);
	CHECK_NEAR(meter.shortTerm(), -23.0, kTolerance);
	CHECK_NEAR(meter.integrated(), -23.0, kTolerance);
}

TEST_CASE("loudness", tech3341_case2_sine_minus33)
{
	const std::vector<float> tone = signals::sine(kRate, 997.0, 20.0, -33.0);
	LoudnessMeter meter = stereoMeter();
	measure(meter, {tone, tone});
	CHECK_NEAR(meter.momentary(), -33.0, kTolerance);
	CHECK_NEAR(meter.shortTerm(), -33.0, kTolerance);
	CHECK_NEAR(meter.integrated(), -33.0, kTolerance);
}

TEST_CASE("loudness", tech3341_case3_relative_gate)
{
	// −36 / −23 / −36 dBFS: das relative Gate blendet die leisen Teile aus
	const auto signal = signals::steppedSine(kRate, 997.0, {{10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}});
	CHECK_NEAR(integratedStereo(signal), -23.0, kTolerance);
}

TEST_CASE("loudness", tech3341_case4_absolute_gate)
{
	const auto signal = signals::steppedSine(
		kRate, 997.0, {{10.0, -72.0}, {10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}, {10.0, -72.0}});
	CHECK_NEAR(integratedStereo(signal), -23.0, kTolerance);
}

TEST_CASE("loudness", tech3341_case5_mixed_levels)
{
	const auto signal = signals::steppedSine(kRate, 997.0, {{20.0, -26.0}, {20.1, -20.0}, {20.0, -26.0}});
	CHECK_NEAR(integratedStereo(signal), -23.0, kTolerance);
}

TEST_CASE("loudness", tech3341_case6_five_channels)
{
	// 5.0: L und R −28 dBFS, C −24 dBFS, Ls und Rs −30 dBFS (Surround mit Gewicht 1,41)
	const auto front = signals::sine(kRate, 997.0, 20.0, -28.0);
	const auto center = signals::sine(kRate, 997.0, 20.0, -24.0);
	const auto surround = signals::sine(kRate, 997.0, 20.0, -30.0);
	LoudnessMeter meter;
	meter.configure(kRate, {1.0f, 1.0f, 1.0f, 1.41f, 1.41f});
	measure(meter, {front, front, center, surround, surround});
	CHECK_NEAR(meter.integrated(), -23.0, kTolerance);
}

TEST_CASE("loudness", short_term_constant_with_bursts)
{
	// Nach dem Short-term-Fall aus Tech 3341: 1,34 s bei −20 dBFS und 1,66 s bei −30 dBFS im Wechsel. Das
	// 3-s-Fenster enthält immer genau eine Periode, S bleibt bei −23,0 LUFS.
	std::vector<std::pair<double, double>> steps;
	for (int i = 0; i < 5; i++) {
		steps.push_back({1.34, -20.0});
		steps.push_back({1.66, -30.0});
	}
	const auto signal = signals::steppedSine(kRate, 997.0, steps);
	LoudnessMeter meter = stereoMeter();
	float low = 0.0f, high = -200.0f;
	measure(meter, {signal, signal}, [&](const LoudnessMeter &m) {
		if (m.blocks() >= 30) {
			low = std::min(low, m.shortTerm());
			high = std::max(high, m.shortTerm());
		}
	});
	CHECK_NEAR(low, -23.0, kTolerance);
	CHECK_NEAR(high, -23.0, kTolerance);
}

TEST_CASE("loudness", momentary_constant_with_bursts)
{
	// Nach dem Momentary-Fall aus Tech 3341: 0,18 s bei −20 dBFS und 0,22 s bei −30 dBFS im Wechsel
	std::vector<std::pair<double, double>> steps;
	for (int i = 0; i < 25; i++) {
		steps.push_back({0.18, -20.0});
		steps.push_back({0.22, -30.0});
	}
	const auto signal = signals::steppedSine(kRate, 997.0, steps);
	LoudnessMeter meter = stereoMeter();
	float low = 0.0f, high = -200.0f;
	measure(meter, {signal, signal}, [&](const LoudnessMeter &m) {
		if (m.blocks() >= 4) {
			low = std::min(low, m.momentary());
			high = std::max(high, m.momentary());
		}
	});
	CHECK_NEAR(low, -23.0, kTolerance);
	CHECK_NEAR(high, -23.0, kTolerance);
}

TEST_CASE("loudness", other_sample_rates)
{
	// Beliebige Abtastraten: der K-Filter wird für jede Rate neu berechnet
	for (const double rate : {32000.0, 44100.0, 88200.0, 96000.0}) {
		const std::vector<float> tone = signals::sine(rate, 997.0, 10.0, -23.0);
		LoudnessMeter meter = stereoMeter(rate);
		measure(meter, {tone, tone});
		CHECK_NEAR(meter.shortTerm(), -23.0, kTolerance);
		CHECK_NEAR(meter.integrated(), -23.0, kTolerance);
	}
}

TEST_CASE("loudness", mono_and_surround_layouts)
{
	// Mono zählt einen Kanal: 3 dB leiser als derselbe Ton auf zwei Kanälen
	const std::vector<float> tone = signals::sine(kRate, 997.0, 5.0, -23.0);
	LoudnessMeter mono;
	mono.configure(kRate, channelWeights(1));
	measure(mono, {tone});
	CHECK_NEAR(mono.shortTerm(), -26.01, kTolerance);

	// 5.1: LFE zählt nicht mit
	const std::vector<float> silence(tone.size(), 0.0f);
	LoudnessMeter surround;
	surround.configure(kRate, channelWeights(6));
	measure(surround, {tone, tone, silence, tone, silence, silence});
	CHECK_NEAR(surround.shortTerm(), -23.0, kTolerance);
}

TEST_CASE("loudness", silence_is_minus_infinity)
{
	const std::vector<float> silence(signals::samples(kRate, 5.0), 0.0f);
	LoudnessMeter meter = stereoMeter();
	measure(meter, {silence, silence});
	CHECK(isSilent(meter.momentary()));
	CHECK(isSilent(meter.shortTerm()));
	CHECK(isSilent(meter.integrated()));
	CHECK_NEAR(meter.loudnessRange(), 0.0, 1e-9);
}

TEST_CASE("loudness", tech3342_loudness_range)
{
	// EBU Tech 3342, Fälle 1–4: LRA mit ±1 LU
	CHECK_NEAR(loudnessRange(signals::steppedSine(kRate, 1000.0, {{20.0, -20.0}, {20.0, -30.0}})), 10.0, 1.0);
	CHECK_NEAR(loudnessRange(signals::steppedSine(kRate, 1000.0, {{20.0, -20.0}, {20.0, -15.0}})), 5.0, 1.0);
	CHECK_NEAR(loudnessRange(signals::steppedSine(kRate, 1000.0, {{20.0, -40.0}, {20.0, -20.0}})), 20.0, 1.0);
	CHECK_NEAR(loudnessRange(signals::steppedSine(
			   kRate, 1000.0, {{20.0, -50.0}, {20.0, -35.0}, {20.0, -20.0}, {20.0, -35.0}, {20.0, -50.0}})),
		   15.0, 1.0);
}

TEST_CASE("loudness", block_timing)
{
	// Genau zehn Blöcke pro Sekunde, auch bei 44,1 kHz
	const std::vector<float> tone = signals::sine(44100.0, 997.0, 3.0, -23.0);
	LoudnessMeter meter = stereoMeter(44100.0);
	measure(meter, {tone, tone});
	CHECK(meter.blocks() == 30);
}
