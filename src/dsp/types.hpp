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

#include <cmath>
#include <cstdint>

// Gemeinsame Grundlagen des DSP-Kerns. Der Kern hängt weder von OBS noch von Qt ab, damit er sich
// eigenständig testen lässt.
namespace am::dsp {

constexpr double kPi = 3.14159265358979323846;

// Untergrenze für Lautheit und Pegel: steht für „Stille“ (minus unendlich)
constexpr float kSilence = -120.0f;

inline bool isSilent(float value)
{
	return value <= kSilence + 1.0f;
}

inline float linearToDb(double value)
{
	return value > 1e-6 ? static_cast<float>(20.0 * std::log10(value)) : kSilence;
}

inline double dbToLinear(double db)
{
	return std::pow(10.0, db / 20.0);
}

// Lautheit aus der mittleren K-gewichteten Leistung (ITU-R BS.1770)
inline float powerToLufs(double power)
{
	return power > 1e-12 ? static_cast<float>(-0.691 + 10.0 * std::log10(power)) : kSilence;
}

inline double lufsToPower(double lufs)
{
	return std::pow(10.0, (lufs + 0.691) / 10.0);
}

// Rolle einer Audioquelle im Mix
enum class Role : int { Voice = 0, Game, Chat, Music, Alerts, Desktop, Ignore };
constexpr int kRoleCount = 7;

const char *roleId(Role role); // „voice“, „game“ …: Schlüssel in JSON-Dateien
bool roleFromId(const char *id, Role &role);

// Rollen, deren Pegel über die Spracherkennung gemessen wird
inline bool isSpeechRole(Role role)
{
	return role == Role::Voice || role == Role::Chat;
}

} // namespace am::dsp
