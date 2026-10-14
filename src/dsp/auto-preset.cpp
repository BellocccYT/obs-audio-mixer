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

#include "auto-preset.hpp"

#include <algorithm>

namespace am::dsp {

void AutoPresetSelector::reset(const std::string &current)
{
	current_ = current;
	candidate_.clear();
	candidateFor_ = 0.0f;
	sinceSwitch_ = config_.minDwellSeconds; // der erste Wechsel darf nach der Anlaufzeit kommen
	observed_ = 0.0f;
	game_ = music_ = voice_ = chat_ = 0.0f;
}

std::string AutoPresetSelector::suggestion() const
{
	if (chat_ >= 0.20f && voice_ >= 0.15f && game_ < 0.30f)
		return preset_ids::kPodcast; // du und Gäste reden viel, kaum Spiel
	if (music_ >= 0.50f && game_ < 0.25f)
		return preset_ids::kMusic;
	if (game_ < 0.10f && music_ < 0.40f)
		return preset_ids::kJustChatting; // lange kein Spielton
	if (game_ >= 0.40f && voice_ >= 0.25f)
		return preset_ids::kVoiceFirst; // viel Sprechen über Spielton
	if (game_ >= 0.50f && voice_ < 0.08f)
		return preset_ids::kGameFirst; // Spiel läuft, du sprichst kaum (Story, Atmosphäre)
	return {};
}

const std::string &AutoPresetSelector::update(float dt, const Activity &activity)
{
	// Anteile der letzten Minuten (gleitender Mittelwert, am Anfang einfacher Mittelwert)
	observed_ += dt;
	const float alpha = std::min(1.0f, dt / std::min(config_.windowSeconds, std::max(observed_, 1.0f)));
	game_ += ((activity.game ? 1.0f : 0.0f) - game_) * alpha;
	music_ += ((activity.music ? 1.0f : 0.0f) - music_) * alpha;
	voice_ += ((activity.voice ? 1.0f : 0.0f) - voice_) * alpha;
	chat_ += ((activity.chat ? 1.0f : 0.0f) - chat_) * alpha;
	sinceSwitch_ += dt;

	if (observed_ < config_.warmupSeconds)
		return current_;

	const std::string wanted = suggestion();
	if (wanted.empty() || wanted == current_) {
		candidate_.clear();
		candidateFor_ = 0.0f;
		return current_;
	}
	if (wanted != candidate_) {
		candidate_ = wanted;
		candidateFor_ = 0.0f;
	}
	candidateFor_ += dt;
	if (candidateFor_ >= config_.switchDelaySeconds && sinceSwitch_ >= config_.minDwellSeconds) {
		current_ = candidate_;
		candidate_.clear();
		candidateFor_ = 0.0f;
		sinceSwitch_ = 0.0f;
	}
	return current_;
}

} // namespace am::dsp
