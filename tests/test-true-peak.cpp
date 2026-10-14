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

// True Peak nach den Fällen aus EBU Tech 3341 (Sinus bei fs/4, fs/6, fs/8 mit Phasenversatz, sodass die
// Spitze zwischen den Samples liegt). Toleranz laut Tech 3341: +0,2 / −0,4 dB.

#include "signals.hpp"
#include "test-framework.hpp"

#include <dsp/true-peak.hpp>

using namespace am::dsp;

namespace {

// Die ersten 100 ms zählen nicht: der harte Einsatz aus der Stille (Sprung von 0 auf den Sinus) erzeugt
// beim Überabtasten ein Überschwingen, das im Signal selbst nicht vorkommt
float truePeakDb(const std::vector<float> &mono, float gate = 0.0f)
{
	TruePeakMeter meter;
	meter.configure(2, gate);
	float peak = 0.0f;
	for (size_t pos = 0; pos < mono.size(); pos += 1200) {
		const float *data[2] = {mono.data() + pos, mono.data() + pos};
		const float block = meter.process(data, std::min<size_t>(1200, mono.size() - pos));
		if (pos >= 4800)
			peak = std::max(peak, block);
	}
	return linearToDb(peak);
}

float samplePeakDb(const std::vector<float> &mono)
{
	float peak = 0.0f;
	for (const float sample : mono)
		peak = std::max(peak, std::fabs(sample));
	return linearToDb(peak);
}

} // namespace

TEST_CASE("true-peak", quarter_rate_45_degrees)
{
	// 12 kHz bei 48 kHz, 45° versetzt: Samples liegen 3 dB unter der wahren Spitze
	const auto tone = signals::sine(48000.0, 12000.0, 1.0, -6.0, 45.0);
	CHECK_NEAR(samplePeakDb(tone), -9.01, 0.05);
	CHECK_RANGE(truePeakDb(tone), -6.4, -5.8);
}

TEST_CASE("true-peak", sixth_rate_60_degrees)
{
	const auto tone = signals::sine(48000.0, 8000.0, 1.0, -6.0, 60.0);
	CHECK_RANGE(truePeakDb(tone), -6.4, -5.8);
}

TEST_CASE("true-peak", eighth_rate_67_degrees)
{
	const auto tone = signals::sine(48000.0, 6000.0, 1.0, -6.0, 67.5);
	CHECK_RANGE(truePeakDb(tone), -6.4, -5.8);
}

TEST_CASE("true-peak", above_full_scale)
{
	// Samples bei 0 dBFS, wahre Spitze bei +3 dBTP
	const auto tone = signals::sine(48000.0, 12000.0, 1.0, 3.01, 45.0);
	CHECK_NEAR(samplePeakDb(tone), 0.0, 0.05);
	CHECK_RANGE(truePeakDb(tone), 2.6, 3.2);
}

TEST_CASE("true-peak", low_frequency_matches_sample_peak)
{
	const auto tone = signals::sine(48000.0, 997.0, 1.0, -6.0);
	CHECK_NEAR(truePeakDb(tone), -6.0, 0.1);
}

TEST_CASE("true-peak", sample_rate_44100)
{
	const auto tone = signals::sine(44100.0, 11025.0, 1.0, -6.0, 45.0);
	CHECK_RANGE(truePeakDb(tone), -6.4, -5.8);
}

TEST_CASE("true-peak", gate_skips_quiet_parts)
{
	// Unter der Schwelle meldet die Messung den Sample-Peak, darüber den echten True Peak
	const auto quiet = signals::sine(48000.0, 12000.0, 0.5, -26.0, 45.0);
	CHECK_NEAR(truePeakDb(quiet, 0.25f), samplePeakDb(quiet), 0.01);
	const auto loud = signals::sine(48000.0, 12000.0, 0.5, -6.0, 45.0);
	CHECK_RANGE(truePeakDb(loud, 0.25f), -6.4, -5.8);
}
