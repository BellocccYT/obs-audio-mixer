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

#include "true-peak.hpp"

#include <cstddef>
#include <vector>

namespace am::dsp {

// Ausgangsstufe des Filters: wendet die Verstärkung sample-genau und geglättet an (keine Knackser) und
// begrenzt danach mit einem True-Peak-Limiter mit kurzem Lookahead. So übersteuert auch ein Hochregeln nie.
//
// Limiter: Für jedes Sample wird die nötige Absenkung aus dem überabgetasteten Spitzenwert bestimmt,
// über die Lookahead-Länge das Minimum gehalten und danach über dieselbe Länge gemittelt. Weil jeder
// gemittelte Wert schon kleiner oder gleich der nötigen Absenkung ist, liegt die Ausgabe sicher unter der
// Obergrenze, und die Absenkung setzt weich ein.
class OutputStage {
public:
	struct Config {
		double sampleRate = 48000.0;
		int channels = 2;
		float lookaheadMs = 1.5f;
		float releaseMs = 150.0f;
		float smoothingMs = 15.0f; // Glättung der Verstärkung (Attack des Schreischutzes ca. 50 ms)
	};

	void configure(const Config &config);
	void reset();

	// In-place. gainDb: gewünschte Verstärkung, limiter: True-Peak-Limiter an, ceilingDb: Obergrenze in dBTP
	void process(float *const *data, size_t frames, float gainDb, bool limiter, float ceilingDb);

	float gainDb() const;                       // aktuell angewendete Verstärkung
	float gainReductionDb() const;              // stärkste Absenkung des Limiters im letzten Abschnitt (≤ 0)
	size_t latency() const { return latency_; } // Verzögerung in Samples

private:
	struct Channel {
		std::vector<float> delay; // Verzögerungsleitung (Ring)
		SampleHistory history;    // für die Überabtastung
		int countdown = 0;
	};

	void resetLimiter();

	Config config_;
	Oversampler4x oversampler_;
	std::vector<Channel> channels_;
	size_t window_ = 72;  // Lookahead in Samples
	size_t latency_ = 79; // kDelay der Überabtastung + window_ − 1
	size_t delaySize_ = 80;
	size_t delayHead_ = 0;

	double gain_ = 1.0;        // geglättete Verstärkung (linear)
	double smoothing_ = 0.001; // Glättungsfaktor pro Sample
	double release_ = 0.0001;  // Release-Faktor pro Sample

	// Gleitendes Minimum der nötigen Absenkung (monotone Warteschlange als Ring)
	std::vector<float> minValues_;
	std::vector<size_t> minIndices_;
	size_t minHead_ = 0;
	size_t minCount_ = 0;
	size_t sampleIndex_ = 0;

	// Gleitender Mittelwert der Hüllkurve
	std::vector<float> envelope_;
	size_t envelopeHead_ = 0;
	double envelopeSum_ = 0.0;
	float released_ = 1.0f;
	float minGain_ = 1.0f;
	bool limiterIdle_ = true;
};

} // namespace am::dsp
