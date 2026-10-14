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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace am::dsp {

// Überabtastung um den Faktor 4 mit einem Polyphasen-Tiefpass (gefensterter Sinc, 65 Taps). Phase 0 ist
// das um kDelay Samples verzögerte Original, die Phasen 1–3 liegen bei +¼, +½ und +¾ Sample.
class Oversampler4x {
public:
	static constexpr int kTapsPerPhase = 17;
	static constexpr int kDelay = 8;

	Oversampler4x();

	// Zwischenwerte (Phasen 1–3) für das Sample, dessen Verlauf in history endet. history zeigt auf
	// kTapsPerPhase aufeinanderfolgende Samples, das neueste zuletzt.
	void interpolate(const float *history, float out[3]) const;

private:
	std::array<std::array<float, kTapsPerPhase>, 3> phases_{};
};

// Verlauf der letzten Samples eines Kanals als Ring mit doppeltem Speicher: so liegen die letzten
// kTapsPerPhase Werte immer am Stück im Speicher.
class SampleHistory {
public:
	static constexpr int kSize = Oversampler4x::kTapsPerPhase;

	void reset();
	void push(float sample)
	{
		buffer_[static_cast<size_t>(head_)] = sample;
		buffer_[static_cast<size_t>(head_ + kSize)] = sample;
		head_ = head_ + 1 == kSize ? 0 : head_ + 1;
	}
	// Die letzten kSize Samples, das neueste zuletzt
	const float *window() const { return buffer_.data() + head_; }
	// Sample, das delay Schritte zurückliegt (0 = neuestes)
	float at(int delay) const { return buffer_[static_cast<size_t>(head_ + kSize - 1 - delay)]; }

private:
	std::array<float, 2 * kSize> buffer_{};
	int head_ = 0;
};

// True-Peak-Messung nach ITU-R BS.1770-4 (4-fache Überabtastung). Damit die Messung kaum Rechenzeit
// kostet, wird nur in der Nähe von Samples über gate überabgetastet; leisere Spitzen meldet sie als
// Sample-Peak. gate = 0 misst überall.
class TruePeakMeter {
public:
	void configure(int channels, float gate);
	void reset();

	// Höchster Betrag (linear) in diesem Abschnitt, überabgetastet. Optional im selben Durchlauf: höchster
	// Sample-Betrag und Zahl der Samples ab clipLevel
	float process(const float *const *data, size_t frames, float *samplePeak = nullptr, uint64_t *clipped = nullptr,
		      float clipLevel = 0.9999f);

private:
	Oversampler4x oversampler_;
	std::vector<SampleHistory> history_;
	std::vector<int> countdown_;
	float gate_ = 0.0f;
};

} // namespace am::dsp
