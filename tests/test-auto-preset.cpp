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

// Preset „Automatisch“: Wechsel nach Aktivität, nur mit Verzögerung und Hysterese

#include "test-framework.hpp"

#include <dsp/auto-preset.hpp>

#include <functional>

using namespace am::dsp;

namespace {

constexpr float kDt = 0.1f;

// Lässt die Auswahl seconds Sekunden laufen; activity bekommt die Zeit seit Beginn dieses Abschnitts
std::string runFor(AutoPresetSelector &selector, float seconds, const std::function<Activity(float)> &activity)
{
	std::string current = selector.current();
	for (float t = 0.0f; t < seconds; t += kDt)
		current = selector.update(kDt, activity(t));
	return current;
}

Activity talkingOverGame(float t)
{
	Activity a;
	a.game = true;
	a.voice = static_cast<int>(t) % 3 != 0; // zwei Drittel der Zeit wird gesprochen
	return a;
}

Activity justTalking(float t)
{
	Activity a;
	a.voice = static_cast<int>(t) % 2 == 0;
	return a;
}

} // namespace

TEST_CASE("auto-preset", no_game_switches_to_just_chatting_with_delay)
{
	AutoPresetSelector selector;
	selector.reset(preset_ids::kVoiceFirst);
	CHECK(runFor(selector, 120.0f, talkingOverGame) == preset_ids::kVoiceFirst);
	// Spiel aus: die Spielanteile sinken langsam; nach 20 s wird noch nicht gewechselt
	CHECK(runFor(selector, 20.0f, justTalking) == preset_ids::kVoiceFirst);
	// … aber nach einigen Minuten ohne Spielton
	CHECK(runFor(selector, 300.0f, justTalking) == preset_ids::kJustChatting);
}

TEST_CASE("auto-preset", short_bursts_do_not_flap)
{
	AutoPresetSelector selector;
	selector.reset(preset_ids::kJustChatting);
	runFor(selector, 300.0f, justTalking);
	// 10 s Spielton zwischendurch ändern nichts
	CHECK(runFor(selector, 10.0f, talkingOverGame) == preset_ids::kJustChatting);
	CHECK(runFor(selector, 60.0f, justTalking) == preset_ids::kJustChatting);
}

TEST_CASE("auto-preset", game_without_talking)
{
	AutoPresetSelector selector;
	selector.reset(preset_ids::kVoiceFirst);
	const std::string result = runFor(selector, 400.0f, [](float) {
		Activity a;
		a.game = true;
		return a;
	});
	CHECK(result == preset_ids::kGameFirst);
}

TEST_CASE("auto-preset", minimum_dwell_time)
{
	// Nach einem Wechsel bleibt das Preset mindestens zwei Minuten, auch wenn sich alles ändert
	AutoPresetSelector selector;
	selector.reset(preset_ids::kVoiceFirst);
	float t = 0.0f;
	while (selector.current() == preset_ids::kVoiceFirst && t < 400.0f) {
		selector.update(kDt, justTalking(t));
		t += kDt;
	}
	CHECK(selector.current() == preset_ids::kJustChatting);
	// Direkt danach läuft wieder ein Spiel mit viel Sprechen: 110 s lang kein Wechsel …
	CHECK(runFor(selector, 110.0f, talkingOverGame) == preset_ids::kJustChatting);
	// … danach schon
	CHECK(runFor(selector, 60.0f, talkingOverGame) == preset_ids::kVoiceFirst);
}

TEST_CASE("auto-preset", music_and_podcast)
{
	AutoPresetSelector music;
	music.reset();
	CHECK(runFor(music, 400.0f, [](float) {
		      Activity a;
		      a.music = true;
		      return a;
	      }) == preset_ids::kMusic);

	AutoPresetSelector podcast;
	podcast.reset();
	CHECK(runFor(podcast, 400.0f, [](float t) {
		      Activity a;
		      a.voice = static_cast<int>(t) % 2 == 0;
		      a.chat = static_cast<int>(t) % 2 == 1;
		      return a;
	      }) == preset_ids::kPodcast);
}
