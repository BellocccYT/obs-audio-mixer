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

// Spracherkennung mit künstlicher Sprache, Rauschen und Stille

#include "signals.hpp"
#include "test-framework.hpp"

#include <dsp/analyzer.hpp>

using namespace am::dsp;

namespace {

constexpr double kRate = 48000.0;

// Anteil der 100-ms-Blöcke mit erkannter Sprache in [from, to) Sekunden
struct VadRun {
	std::vector<bool> speaking; // pro Block
	double ratio(double from, double to) const
	{
		size_t count = 0, total = 0;
		for (size_t b = static_cast<size_t>(from * 10); b < static_cast<size_t>(to * 10) && b < speaking.size();
		     b++) {
			total++;
			count += speaking[b] ? 1 : 0;
		}
		return total ? static_cast<double>(count) / static_cast<double>(total) : 0.0;
	}
};

VadRun run(const std::vector<float> &mono)
{
	Analyzer analyzer;
	analyzer.configure(kRate, 2);
	VadRun result;
	for (size_t pos = 0; pos < mono.size(); pos += 1024) {
		const float *data[2] = {mono.data() + pos, mono.data() + pos};
		analyzer.process(data, std::min<size_t>(1024, mono.size() - pos));
		if (analyzer.takeNewBlock())
			result.speaking.push_back(analyzer.snapshot().speaking);
	}
	return result;
}

} // namespace

TEST_CASE("vad", speech_over_quiet_noise)
{
	// 2 s Ruhe, dann abwechselnd 4 s Sprechen und 4 s Pause, darunter leises Rauschen
	std::vector<float> voice(signals::samples(kRate, 2.0), 0.0f);
	for (uint32_t i = 0; i < 2; i++) {
		signals::append(voice, signals::speech(kRate, 4.0, 7 + i));
		voice.resize(voice.size() + signals::samples(kRate, 4.0), 0.0f);
	}
	signals::scaleTo(kRate, voice, -22.0f);
	auto noise = signals::pinkNoise(kRate, 18.0, 3);
	signals::scaleTo(kRate, noise, -60.0f);
	for (size_t i = 0; i < voice.size(); i++)
		voice[i] += noise[i];

	const VadRun result = run(voice);
	CHECK(result.ratio(0.0, 2.0) < 0.05);
	// Während des Sprechens (Silben mit kurzen Lücken) fast durchgehend Sprache
	CHECK(result.ratio(2.5, 5.8) > 0.75);
	CHECK(result.ratio(10.5, 13.8) > 0.75);
	// In den Pausen danach keine Sprache
	CHECK(result.ratio(6.5, 9.8) < 0.1);
	CHECK(result.ratio(14.5, 17.8) < 0.1);
}

TEST_CASE("vad", silence_is_not_speech)
{
	const std::vector<float> silence(signals::samples(kRate, 5.0), 0.0f);
	CHECK(run(silence).ratio(0.0, 5.0) == 0.0);
}

TEST_CASE("vad", steady_noise_is_learned_as_floor)
{
	// Ein Lüfter springt an: kurz kann das wie Sprache aussehen, nach ein paar Sekunden nicht mehr
	std::vector<float> signal(signals::samples(kRate, 2.0), 0.0f);
	auto noise = signals::pinkNoise(kRate, 10.0, 11);
	signals::scaleTo(kRate, noise, -40.0f);
	signals::append(signal, noise);
	const VadRun result = run(signal);
	CHECK(result.ratio(7.0, 12.0) < 0.05);
}

TEST_CASE("vad", quiet_speech_below_minimum_is_ignored)
{
	auto voice = signals::speech(kRate, 6.0, 5);
	signals::scaleTo(kRate, voice, -65.0f);
	CHECK(run(voice).ratio(0.0, 6.0) < 0.05);
}

TEST_CASE("vad", speech_level_ignores_pauses)
{
	// Der Sprachpegel misst nur die Sprachabschnitte: Pausen ziehen ihn nicht nach unten
	auto voice = signals::speech(kRate, 30.0, 9, 2.0, 4.0, 1.0);
	signals::scaleTo(kRate, voice, -24.0f);
	Analyzer analyzer;
	analyzer.configure(kRate, 2);
	for (size_t pos = 0; pos < voice.size(); pos += 1024) {
		const float *data[2] = {voice.data() + pos, voice.data() + pos};
		analyzer.process(data, std::min<size_t>(1024, voice.size() - pos));
	}
	// Integriert (mit Pausen, aber Gate) und Sprachpegel liegen nah beieinander, Short-term in der Pause nicht
	const float integrated = analyzer.meter().integrated();
	CHECK_NEAR(analyzer.snapshot().speech, integrated, 2.5);
}
