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

// Einmess-Assistent: Startwerte aus 30 s Messung

#include "test-framework.hpp"

#include <dsp/calibration.hpp>

using namespace am::dsp;

TEST_CASE("calibration", suggests_gain_for_voice)
{
	CalibrationCollector collector;
	ChannelInput in;
	in.role = Role::Voice;
	in.present = true;
	in.truePeak = -12.0f;
	in.noiseFloor = -70.0f;
	for (int i = 0; i < 600; i++) {
		in.speaking = i % 3 != 0; // zwei Drittel Sprache
		in.speech = -22.0f;
		in.momentary = in.speaking ? -22.0f : -60.0f;
		collector.add(0.05f, in);
	}
	const CalibrationResult result = collector.result(-16.0f, in);
	CHECK(result.enoughData);
	CHECK_NEAR(result.measuredLufs, -22.0, 0.01);
	CHECK_NEAR(result.activeSeconds, 20.0, 0.1);
	CHECK_NEAR(result.suggestedGainDb, 6.0, 0.01);
	CHECK(result.hints == 0);

	// Übernehmen setzt den Regler direkt auf den Vorschlag
	ChannelState state;
	CalibrationCollector::apply(result, state);
	CHECK_NEAR(state.levelGain, 6.0, 0.01);
	CHECK_NEAR(state.estimate, -22.0, 0.01);
}

TEST_CASE("calibration", hints)
{
	CalibrationCollector collector;
	ChannelInput in;
	in.role = Role::Voice;
	in.present = true;
	in.truePeak = -0.2f;    // übersteuert schon vor OBS
	in.noiseFloor = -45.0f; // rauscht
	in.speaking = true;
	in.speech = -50.0f; // viel zu leise
	for (int i = 0; i < 200; i++)
		collector.add(0.05f, in);
	const CalibrationResult result = collector.result(-16.0f, in);
	CHECK(result.hints & kHintClipping);
	CHECK(result.hints & kHintNoisy);
	CHECK(result.hints & kHintTooQuiet);
	CHECK_NEAR(result.suggestedGainDb, 12.0, 1e-6);
}

TEST_CASE("calibration", not_enough_data)
{
	CalibrationCollector collector;
	ChannelInput in;
	in.role = Role::Game;
	in.present = true;
	in.momentary = -70.0f; // still
	for (int i = 0; i < 600; i++)
		collector.add(0.05f, in);
	const CalibrationResult result = collector.result(-26.0f, in);
	CHECK(!result.enoughData);
	CHECK(result.hints & kHintNoSignal);
}
