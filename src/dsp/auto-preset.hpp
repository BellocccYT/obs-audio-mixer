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

#include <string>

namespace am::dsp {

// IDs der mitgelieferten Presets (siehe data/presets.json)
namespace preset_ids {
constexpr const char *kVoiceFirst = "voice-first";
constexpr const char *kGameFirst = "game-first";
constexpr const char *kBalanced = "balanced";
constexpr const char *kCompetitive = "competitive";
constexpr const char *kJustChatting = "just-chatting";
constexpr const char *kMusic = "music";
constexpr const char *kPodcast = "podcast";
constexpr const char *kAuto = "auto";
} // namespace preset_ids

// Was gerade im Mix passiert (pro Regelschritt)
struct Activity {
	bool game = false;  // Spiel (oder Desktop) hörbar
	bool music = false; // Musik hörbar
	bool voice = false; // du sprichst
	bool chat = false;  // Discord/Mitspieler sprechen
};

// Wählt im Modus „Automatisch“ ein Preset anhand der Aktivität der letzten Minuten. Gewechselt wird nur,
// wenn ein anderes Preset eine Weile am Stück besser passt, und nie kurz nach dem letzten Wechsel.
class AutoPresetSelector {
public:
	struct Config {
		float windowSeconds = 90.0f; // über diesen Zeitraum werden die Anteile gemittelt
		float warmupSeconds = 30.0f; // vorher wird nichts gewechselt
		float switchDelaySeconds = 30.0f;
		float minDwellSeconds = 120.0f;
	};

	void setConfig(const Config &config) { config_ = config; }
	void reset(const std::string &current = preset_ids::kVoiceFirst);

	// Liefert das Preset, das gerade gelten soll
	const std::string &update(float dt, const Activity &activity);

	// Das Preset, das zur Aktivität passt (ohne Verzögerung); leer, wenn keins klar besser passt
	std::string suggestion() const;

	const std::string &current() const { return current_; }
	float gameRatio() const { return game_; }
	float musicRatio() const { return music_; }
	float voiceRatio() const { return voice_; }
	float chatRatio() const { return chat_; }

private:
	Config config_;
	std::string current_ = preset_ids::kVoiceFirst;
	std::string candidate_;
	float candidateFor_ = 0.0f;
	float sinceSwitch_ = 120.0f;
	float observed_ = 0.0f;
	float game_ = 0.0f;
	float music_ = 0.0f;
	float voice_ = 0.0f;
	float chat_ = 0.0f;
};

} // namespace am::dsp
