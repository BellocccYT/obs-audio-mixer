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

enum class Status : int { Inactive = 0, Ok, TooLoud, TooQuiet, Clipping };

struct StatusConfig {
	float loudLu = 4.0f;      // so weit über dem Ziel ist es zu laut
	float quietLu = 8.0f;     // so weit unter dem Ziel ist es zu leise
	float loudSeconds = 3.0f; // erst nach dieser Dauer warnen
	float quietSeconds = 6.0f;
	float clipDbtp = -0.5f;       // ab hier gilt ein Spitzenwert als übersteuert
	float clipHoldSeconds = 3.0f; // so lange bleibt die Übersteuerungs-Warnung stehen
	float clearSeconds = 1.0f;    // so lange muss es wieder passen, bevor die Warnung verschwindet
};

// Bewertet eine Quelle oder Tonspur und warnt erst, wenn ein Problem eine Mindestdauer anhält
class StatusTracker {
public:
	void setConfig(const StatusConfig &config) { config_ = config; }
	void reset();

	// effective: Short-term-Lautheit im Mix (LUFS), truePeak im Mix (dBTP), clipped: seit dem letzten
	// Aufruf übersteuerte Samples
	Status update(float dt, bool active, float effective, float target, float truePeak, bool clipped);
	Status status() const { return status_; }

private:
	StatusConfig config_;
	Status status_ = Status::Inactive;
	float loudFor_ = 0.0f;
	float quietFor_ = 0.0f;
	float okFor_ = 0.0f;
	float inactiveFor_ = 0.0f;
	float clipHold_ = 0.0f;
};

} // namespace am::dsp
