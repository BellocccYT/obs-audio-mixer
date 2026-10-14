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

#include "true-peak.hpp"

#include "types.hpp"

#include <algorithm>
#include <cmath>

namespace am::dsp {

namespace {

constexpr int kFactor = 4;
constexpr int kTaps = kFactor * (Oversampler4x::kTapsPerPhase - 1) + 1; // 65
constexpr double kKaiserBeta = 7.0;

// Modifizierte Besselfunktion 0. Ordnung (für das Kaiser-Fenster)
double besselI0(double x)
{
	double sum = 1.0;
	double term = 1.0;
	for (int k = 1; k < 30; k++) {
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

} // namespace

Oversampler4x::Oversampler4x()
{
	const double center = (kTaps - 1) / 2.0;
	std::array<double, kTaps> taps{};
	for (int k = 0; k < kTaps; k++) {
		const double t = (k - center) / kFactor;
		const double sinc = t == 0.0 ? 1.0 : std::sin(kPi * t) / (kPi * t);
		const double ratio = (k - center) / center;
		const double window =
			besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / besselI0(kKaiserBeta);
		taps[static_cast<size_t>(k)] = sinc * window;
	}
	for (int p = 1; p < kFactor; p++) {
		double sum = 0.0;
		for (int j = 0; j < kTapsPerPhase; j++) {
			const int k = p + kFactor * j;
			if (k < kTaps)
				sum += taps[static_cast<size_t>(k)];
		}
		for (int j = 0; j < kTapsPerPhase; j++) {
			const int k = p + kFactor * j;
			// Jede Phase auf Verstärkung 1 normieren
			phases_[static_cast<size_t>(p - 1)][static_cast<size_t>(j)] =
				k < kTaps ? static_cast<float>(taps[static_cast<size_t>(k)] / sum) : 0.0f;
		}
	}
}

void Oversampler4x::interpolate(const float *history, float out[3]) const
{
	// history[kTapsPerPhase - 1] ist das neueste Sample x[n], history[0] das älteste x[n - 16]
	for (size_t p = 0; p < 3; p++) {
		const std::array<float, kTapsPerPhase> &c = phases_[p];
		float sum = 0.0f;
		for (int j = 0; j < kTapsPerPhase; j++)
			sum += c[static_cast<size_t>(j)] * history[kTapsPerPhase - 1 - j];
		out[p] = sum;
	}
}

void SampleHistory::reset()
{
	buffer_.fill(0.0f);
	head_ = 0;
}

void TruePeakMeter::configure(int channels, float gate)
{
	history_.assign(static_cast<size_t>(std::max(channels, 0)), SampleHistory());
	countdown_.assign(history_.size(), 0);
	gate_ = gate;
	reset();
}

void TruePeakMeter::reset()
{
	for (SampleHistory &history : history_)
		history.reset();
	std::fill(countdown_.begin(), countdown_.end(), 0);
}

float TruePeakMeter::process(const float *const *data, size_t frames)
{
	float peak = 0.0f;
	for (size_t c = 0; c < history_.size(); c++) {
		if (!data[c])
			continue;
		const float *in = data[c];
		SampleHistory &history = history_[c];
		int countdown = countdown_[c];
		for (size_t i = 0; i < frames; i++) {
			const float x = in[i];
			const float magnitude = std::fabs(x);
			history.push(x);
			peak = std::max(peak, magnitude);
			if (magnitude >= gate_)
				countdown = SampleHistory::kSize;
			if (countdown > 0) {
				countdown--;
				float between[3];
				oversampler_.interpolate(history.window(), between);
				peak = std::max(
					{peak, std::fabs(between[0]), std::fabs(between[1]), std::fabs(between[2])});
			}
		}
		countdown_[c] = countdown;
	}
	return peak;
}

} // namespace am::dsp
