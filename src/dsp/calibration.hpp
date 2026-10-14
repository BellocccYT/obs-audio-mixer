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

#include "regulator.hpp"
#include "types.hpp"

#include <cstdint>

namespace am::dsp {

// Hinweise des Einmess-Assistenten
enum CalibrationHint : uint32_t {
	kHintNoSignal = 1u << 0, // in der Messzeit kaum aktiv
	kHintTooQuiet = 1u << 1, // auch mit voller Verstärkung zu leise
	kHintTooLoud = 1u << 2,  // auch mit voller Absenkung zu laut
	kHintClipping = 1u << 3, // übersteuert schon vor OBS
	kHintNoisy = 1u << 4,    // hoher Rauschboden (Mikrofon)
};

struct CalibrationResult {
	bool enoughData = false;
	float measuredLufs = kSilence; // Grundlautheit am Filtereingang
	float activeSeconds = 0.0f;
	float maxTruePeak = kSilence;
	float noiseFloor = kSilence;
	float suggestedGainDb = 0.0f;
	uint32_t hints = 0;
};

// Sammelt während des Einmessens (ca. 30 s normal sprechen und spielen) die Werte einer Quelle und schlägt
// daraus eine Startverstärkung vor
class CalibrationCollector {
public:
	void reset();
	void add(float dt, const ChannelInput &in, float activityGateLufs = -50.0f);
	CalibrationResult result(float targetLufs, const ChannelInput &in) const;

	// Übernimmt das Ergebnis in den Regler-Zustand, damit die Regelung sofort mit passenden Werten startet
	static void apply(const CalibrationResult &result, ChannelState &state);

private:
	double sum_ = 0.0; // Summe der Lautheitswerte (dB) während Aktivität
	float active_ = 0.0f;
	float maxTruePeak_ = kSilence;
	double floorSum_ = 0.0;
	float floorTime_ = 0.0f;
};

} // namespace am::dsp
