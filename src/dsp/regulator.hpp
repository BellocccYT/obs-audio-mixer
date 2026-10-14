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

#include "preset.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>

namespace am::dsp {

struct RegulatorConfig {
	float referenceLufs = -16.0f;    // Ziel-Lautheit der Stimme aus den Einstellungen; verschiebt alle Presets
	float deadzoneLu = 1.0f;         // kleinere Abweichungen bleiben unangetastet
	float settleLu = 0.3f;           // so nah am Ziel hört eine Anpassung auf
	float screamLu = 8.0f;           // Momentary so weit über dem Ziel löst den Schreischutz aus
	float ceilingDbtp = -1.0f;       // Obergrenze für den True Peak
	float activityGateLufs = -50.0f; // leiser gilt eine Quelle als still
	float guardHoldSeconds = 1.0f;   // so lange hält der Schreischutz nach dem letzten Ausreißer
	float duckAttackDbPerS = 30.0f;  // so schnell wird der Rest beim Sprechen leiser
};

// Messwerte und Zustand einer Quelle für einen Regelschritt. Pegel sind vor der Verstärkung des Filters
// gemessen (Eingang des Filters), aber nach den anderen Filtern der Quelle.
struct ChannelInput {
	Role role = Role::Ignore;
	bool present = false; // Messwerte kommen an
	bool audible = false; // landet im Mix: aktiv im Programm und nicht stumm
	bool locked = false;  // vom Nutzer gesperrt: nichts ändern
	float block = kSilence;
	float momentary = kSilence;
	float shortTerm = kSilence;
	float speech = kSilence;      // Sprachpegel der letzten 3 s Sprache
	float speechBlock = kSilence; // letzter Sprachblock (100 ms), kSilence ohne Sprache
	float truePeak = kSilence;
	float noiseFloor = kSilence; // Rauschboden der Spracherkennung
	bool speaking = false;
	float faderDb = 0.0f; // OBS-Fader; Ziele gelten nach dem Fader
	float minGainDb = -30.0f;
	float maxGainDb = 12.0f;
};

// Ereignisse für den Verlauf im Dock
enum ChannelEvent : uint32_t {
	kEventDuckStart = 1u << 0,
	kEventDuckEnd = 1u << 1,
	kEventGuard = 1u << 2,
	kEventLevelDown = 1u << 3,
	kEventLevelUp = 1u << 4,
};

struct ChannelState {
	// Bestandteile der Verstärkung in dB
	float levelGain = 0.0f;    // gleicht die Grundlautheit an das Ziel an
	float duckGain = 0.0f;     // leiser, während jemand spricht
	float guardGain = 0.0f;    // zusätzliche Absenkung durch den Schreischutz (≤ 0)
	float guardLimit = 100.0f; // Obergrenze der Gesamtverstärkung durch den Schreischutz (100 = keine)

	float estimate = kSilence; // gelernte Grundlautheit am Filtereingang (LUFS)
	double activeSeconds = 0.0;
	float activeRun = 0.0f;      // so lange ist die Quelle schon ohne Unterbrechung aktiv
	float louderSeconds = 0.0f;  // so lange ist es schon deutlich lauter als gelernt
	float quieterSeconds = 0.0f; // … bzw. leiser
	float outlierHold = 0.0f;    // nach einem Ausreißer nicht lernen, bis er aus den Messfenstern ist
	bool relearning = false;     // die Grundlautheit hat sich dauerhaft geändert: schnell nachlernen
	float guardHold = 0.0f;
	bool moving = false;
	bool ducking = false;
	float reportedLevel = 0.0f;

	// Ergebnis des letzten Regelschritts
	float gainDb = 0.0f;         // Gesamtverstärkung für den Filter
	float targetLufs = kSilence; // Ziel laut Preset (nach dem Fader)
	bool active = false;         // gerade aktiv (Sprache bzw. hörbarer Ton)
	uint32_t events = 0;
	float eventDb = 0.0f;

	// Gelerntes vergessen (z. B. „neu einmessen“); die aktuelle Verstärkung bleibt
	void resetLearning()
	{
		estimate = kSilence;
		activeSeconds = 0.0;
		activeRun = 0.0f;
		louderSeconds = quieterSeconds = outlierHold = 0.0f;
		relearning = false;
		moving = false;
	}
};

struct Channel {
	ChannelInput in;
	ChannelState state;
};

// Regelt die Lautheit aller Quellen auf die Ziele des Presets. Läuft 10–20× pro Sekunde außerhalb des
// Audio-Threads. Gesteuert wird vorwärts: aus der gemessenen Eingangslautheit folgt die Verstärkung,
// es gibt keine Rückkopplung über die eigene Verstärkung.
class Regulator {
public:
	void setPreset(const Preset &preset) { preset_ = preset; }
	const Preset &preset() const { return preset_; }
	void setConfig(const RegulatorConfig &config) { config_ = config; }
	const RegulatorConfig &config() const { return config_; }

	// Ziel einer Rolle in LUFS (nach dem Fader), kSilence für „Ignorieren“
	float target(Role role) const;
	bool ducking() const { return duckHold_ > 0.0f; }

	void update(double dt, Channel *const *channels, size_t count);

private:
	void updateChannel(float dt, Channel &channel, bool duck) const;

	Preset preset_;
	RegulatorConfig config_;
	float duckHold_ = 0.0f;
};

} // namespace am::dsp
