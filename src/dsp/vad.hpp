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

#include "types.hpp"

namespace am::dsp {

struct VadConfig {
	float onDb = 9.0f;              // so weit über dem Rauschboden beginnt Sprache
	float offDb = 5.0f;             // darunter endet sie (Hysterese)
	float minLufs = -55.0f;         // leiser ist nie Sprache
	float onsetMs = 30.0f;          // so lange muss es am Stück laut sein
	float hangoverMs = 300.0f;      // so lange gilt Sprache nach dem letzten lauten Abschnitt noch
	float floorRiseDbPerS = 1.5f;   // so schnell steigt der Rauschboden bei wechselndem Pegel
	float steadyRiseDbPerS = 20.0f; // … und so schnell bei gleichmäßigem Geräusch (Lüfter, Rauschen)
};

// Einfache Spracherkennung über Energie und Hysterese. Der Rauschboden wird laufend nachgeführt: Er fällt
// schnell auf leise Stellen (Pausen zwischen Wörtern) und steigt nur langsam. Gleichmäßige Geräusche
// erkennt er an der geringen Schwankung des Pegels und folgt ihnen schneller.
class VoiceActivityDetector {
public:
	void configure(double framesPerSecond, const VadConfig &config = VadConfig());
	void reset();

	// Leistung eines 10-ms-Abschnitts (K-gewichtet, wie im LoudnessMeter)
	void processFrame(double power);

	bool speaking() const { return speaking_; }
	float noiseFloor() const { return floor_; }
	float level() const { return level_; }

private:
	VadConfig config_;
	double framesPerSecond_ = 100.0;
	int onsetFrames_ = 3;
	int hangoverFrames_ = 30;

	float floor_ = -60.0f;
	float level_ = kSilence;
	float mean_ = -100.0f;        // gleitender Mittelwert des Pegels (dB)
	float meanSquare_ = 10100.0f; // … und seines Quadrats, für die Schwankung
	int onset_ = 0;
	int hangover_ = 0;
	bool speaking_ = false;
};

} // namespace am::dsp
