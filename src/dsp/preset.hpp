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

#include <array>
#include <string>

namespace am::dsp {

// Tempo der Anpassung
enum class Speed : int { Slow = 0, Normal, Fast };
constexpr int kSpeedCount = 3;

const char *speedId(Speed speed); // „slow“, „normal“, „fast“
bool speedFromId(const char *id, Speed &speed);

struct SpeedProfile {
	float downDbPerS;         // so schnell wird leiser geregelt
	float upDbPerS;           // so schnell wird lauter geregelt (langsamer, damit nichts pumpt)
	float learnSeconds;       // Zeitkonstante der Langzeit-Mittelung (Spiel, Desktop)
	float duckReleaseDbPerS;  // so schnell kommt der Rest nach dem Sprechen zurück
	float guardReleaseDbPerS; // so schnell löst sich der Schreischutz
};
const SpeedProfile &speedProfile(Speed speed);

// Ein Preset legt fest, wie laut die Rollen zueinander sein sollen. Die Startwerte stehen in
// data/presets.json; die Werte hier sind nur ein neutraler Rückfall.
struct Preset {
	std::string id;
	std::string name;
	std::string description;
	Role anchor = Role::Voice;                   // Hauptrolle, auf sie bezieht sich targetLufs
	float targetLufs = -16.0f;                   // Ziel-Lautheit der Hauptrolle
	std::array<float, kRoleCount> offsets{};     // Abstand jeder Rolle zum Ziel in LU
	float duckDb = 0.0f;                         // so viel leiser wird der Rest, wenn gesprochen wird
	std::array<bool, kRoleCount> ducked{};       // diese Rollen werden beim Sprechen leiser
	std::array<bool, kRoleCount> duckTriggers{}; // Sprache dieser Rollen löst das Ducking aus
	float duckHoldMs = 800.0f;                   // so lange nach dem Sprechen warten, bevor es zurückgeht
	Speed speed = Speed::Normal;
	bool screamGuard = true; // plötzliche Ausreißer sofort absenken (sonst nur der Limiter)
	bool builtin = false;    // mitgeliefert, nicht vom Nutzer

	float offset(Role role) const { return offsets[static_cast<size_t>(role)]; }
	bool isDucked(Role role) const { return ducked[static_cast<size_t>(role)]; }
	bool isDuckTrigger(Role role) const { return duckTriggers[static_cast<size_t>(role)]; }
};

} // namespace am::dsp
