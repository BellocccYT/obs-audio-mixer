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

#include "dsp/status.hpp"
#include "dsp/types.hpp"

#include <string>
#include <vector>

namespace am {

enum class Mode { Observe = 0, Regulate };

// Einstellungen einer Quelle, per UUID gespeichert (der Name dient als Rückfall, z. B. nach dem
// Neuanlegen einer Szenensammlung)
struct SourceSettings {
	std::string uuid;
	std::string name;
	dsp::Role role = dsp::Role::Ignore;
	bool roleChosen = false; // vom Nutzer gewählt (sonst gilt der automatische Vorschlag)
	bool locked = false;
	float minGainDb = -30.0f;
	float maxGainDb = 12.0f;
};

struct ScenePreset {
	std::string scene;
	std::string preset;
};

struct Settings {
	bool enabled = true;
	Mode mode = Mode::Observe;
	std::string preset = "voice-first";
	bool filterConsent = false; // der Nutzer hat erlaubt, den Filter an Quellen zu hängen

	float voiceTargetLufs = -16.0f; // verschiebt alle Presets
	float mixTargetLufs = -14.0f;   // Ziel für den Gesamtmix (Streamspur)
	float mixMaxLufs = -10.0f;      // darüber ist der Gesamtmix zu laut
	float ceilingDbtp = -1.0f;      // True-Peak-Obergrenze des Limiters
	int streamTrack = 0;            // 0 = automatisch (aus dem Stream-Output, sonst Spur 1), sonst 1–6
	bool lockOnFader = true;        // Fader bewegt → Quelle sperren
	float defaultMinGainDb = -30.0f;
	float defaultMaxGainDb = 12.0f;

	dsp::StatusConfig warnings;
	bool muteWarning = true;           // „Du sprichst, aber dein Mikro ist stumm“
	bool desktopHintDismissed = false; // Hinweis „alles läuft über Desktop-Audio“ ausgeblendet

	bool autoByActivity = true; // „Automatisch“ wählt auch nach Aktivität, nicht nur nach Szene
	std::vector<ScenePreset> scenePresets;
	std::vector<SourceSettings> sources;

	bool dockIntroduced = false; // Dock beim allerersten Start einmal einblenden

	SourceSettings *findSource(const std::string &uuid, const std::string &name);
	const ScenePreset *findScene(const std::string &scene) const;
};

Settings loadSettings();
void saveSettings(const Settings &settings);

// Pfad im Konfigurationsordner des Plugins (…/obs-studio/plugin_config/obs-audio-mixer/)
std::string configPath(const char *file);

} // namespace am
