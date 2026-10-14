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

// Regler-Simulation: künstliche Quellen laufen wie im Plugin durch Messung (vor der Verstärkung),
// Regler (20 Hz) und Ausgangsstufe mit Limiter.

#include "signals.hpp"
#include "test-framework.hpp"

#include <dsp/analyzer.hpp>
#include <dsp/output-stage.hpp>
#include <dsp/regulator.hpp>
#include <dsp/true-peak.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace am::dsp;

namespace {

constexpr double kRate = 48000.0;
constexpr size_t kChunk = 1024;
constexpr double kTick = 0.05; // Regler läuft mit 20 Hz

Preset voiceFirst()
{
	Preset p;
	p.id = "voice-first";
	p.anchor = Role::Voice;
	p.targetLufs = -16.0f;
	p.offsets = {0.0f, -10.0f, -1.0f, -18.0f, -4.0f, -10.0f, 0.0f};
	p.duckDb = 4.0f;
	p.ducked[static_cast<size_t>(Role::Game)] = true;
	p.ducked[static_cast<size_t>(Role::Music)] = true;
	p.ducked[static_cast<size_t>(Role::Desktop)] = true;
	p.duckTriggers[static_cast<size_t>(Role::Voice)] = true;
	p.duckHoldMs = 800.0f;
	return p;
}

Preset competitive()
{
	Preset p = voiceFirst();
	p.id = "competitive";
	p.offsets[static_cast<size_t>(Role::Game)] = -7.0f;
	p.duckDb = 0.0f;
	p.speed = Speed::Slow;
	p.screamGuard = false;
	return p;
}

// Eine Quelle der Simulation mit eigener Messung und Ausgangsstufe
struct Source {
	Role role;
	std::vector<float> signal; // Mono, auf beide Kanäle verteilt
	Analyzer analyzer;
	OutputStage stage;
	Channel channel;
	TruePeakMeter outputPeak;
	float maxOutputPeak = 0.0f;
	std::vector<ChannelState> history; // Zustand nach jedem Regelschritt
};

class Simulation {
public:
	explicit Simulation(const Preset &preset) { regulator.setPreset(preset); }

	Source &add(Role role, std::vector<float> signal)
	{
		auto source = std::make_unique<Source>();
		source->role = role;
		source->signal = std::move(signal);
		source->analyzer.configure(kRate, 2);
		OutputStage::Config config;
		config.sampleRate = kRate;
		config.channels = 2;
		source->stage.configure(config);
		source->outputPeak.configure(2, 0.0f);
		source->channel.in.role = role;
		sources.push_back(std::move(source));
		return *sources.back();
	}

	void run(double seconds)
	{
		const size_t total = signals::samples(kRate, seconds);
		std::vector<float> left(kChunk), right(kChunk);
		double sinceTick = 0.0;
		for (size_t pos = 0; pos < total; pos += kChunk) {
			const size_t n = std::min(kChunk, total - pos);
			for (auto &source : sources) {
				for (size_t i = 0; i < n; i++) {
					const size_t index = pos + i;
					left[i] = right[i] = index < source->signal.size() ? source->signal[index]
											   : 0.0f;
				}
				float *data[2] = {left.data(), right.data()};
				source->analyzer.process(data, n);
				const AnalyzerSnapshot &snap = source->analyzer.snapshot();
				ChannelInput &in = source->channel.in;
				in.present = snap.blocks > 0;
				in.audible = true;
				in.block = snap.block;
				in.momentary = snap.momentary;
				in.shortTerm = snap.shortTerm;
				in.speech = snap.speech;
				in.speechBlock = snap.speechBlock;
				in.truePeak = snap.truePeak;
				in.speaking = snap.speaking;
				source->stage.process(data, n, source->channel.state.gainDb, limiter, -1.0f);
				const float *out[2] = {left.data(), right.data()};
				source->maxOutputPeak =
					std::max(source->maxOutputPeak, source->outputPeak.process(out, n));
			}
			sinceTick += static_cast<double>(n) / kRate;
			while (sinceTick >= kTick) {
				sinceTick -= kTick;
				std::vector<Channel *> channels;
				for (auto &source : sources)
					channels.push_back(&source->channel);
				regulator.update(kTick, channels.data(), channels.size());
				for (auto &source : sources)
					source->history.push_back(source->channel.state);
			}
		}
	}

	Regulator regulator;
	bool limiter = true;
	std::vector<std::unique_ptr<Source>> sources;
};

// Mit AM_DUMP=1 zeigt die Simulation den Verlauf (zur Fehlersuche)
void dump(const char *name, const Source &source, double every = 1.0)
{
	if (!std::getenv("AM_DUMP"))
		return;
	std::printf("  -- %s\n", name);
	const size_t step = static_cast<size_t>(every / kTick);
	for (size_t t = 0; t < source.history.size(); t += step) {
		const ChannelState &s = source.history[t];
		std::printf("  t=%5.1f est=%6.1f level=%6.2f duck=%5.2f guard=%6.2f total=%6.2f active=%d\n",
			    static_cast<double>(t) * kTick, static_cast<double>(s.estimate),
			    static_cast<double>(s.levelGain), static_cast<double>(s.duckGain),
			    static_cast<double>(s.guardGain), static_cast<double>(s.gainDb), s.active ? 1 : 0);
	}
}

size_t tickAt(double seconds)
{
	return static_cast<size_t>(seconds / kTick);
}

// Wie oft die Verstärkung zwischen from und to die Richtung wechselt (Maß für Pumpen)
int directionChanges(const Source &source, double from, double to, float (*part)(const ChannelState &))
{
	int changes = 0;
	int direction = 0;
	for (size_t t = tickAt(from) + 1; t < tickAt(to) && t < source.history.size(); t++) {
		const float delta = part(source.history[t]) - part(source.history[t - 1]);
		if (std::fabs(delta) < 1e-4f)
			continue;
		const int now = delta > 0 ? 1 : -1;
		if (direction != 0 && now != direction)
			changes++;
		direction = now;
	}
	return changes;
}

float levelGain(const ChannelState &s)
{
	return s.levelGain;
}

float totalGain(const ChannelState &s)
{
	return s.gainDb;
}

std::vector<float> gameNoise(double seconds, float lufs, uint32_t seed = 1)
{
	auto noise = signals::pinkNoise(kRate, seconds, seed);
	signals::scaleTo(kRate, noise, lufs);
	return noise;
}

std::vector<float> voice(double seconds, float lufs, double talk, double pause, double start, uint32_t seed = 2)
{
	auto speech = signals::speech(kRate, seconds, seed, talk, pause, start);
	// Auf die Lautheit während des Sprechens skalieren (nicht über die Pausen gemittelt)
	auto continuous = signals::speech(kRate, 20.0, seed);
	const float level = signals::stereoLoudness(kRate, continuous);
	const float gain = static_cast<float>(std::pow(10.0, (lufs - level) / 20.0));
	for (float &sample : speech)
		sample *= gain;
	return speech;
}

} // namespace

TEST_CASE("regulator", converges_without_pumping)
{
	// Gleichmäßiger Spielton bei −18 LUFS; Ziel für Spiel im Preset: −16 − 10 = −26 LUFS
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, gameNoise(60.0, -18.0f));
	sim.run(60.0);
	dump("game", game);

	const ChannelState &end = game.history.back();
	CHECK_NEAR(end.levelGain, -8.0, 1.0);
	CHECK_NEAR(end.targetLufs, -26.0, 1e-6);
	// Nach dem Einschwingen bleibt die Verstärkung ruhig
	float low = 100.0f, high = -100.0f;
	for (size_t t = tickAt(20.0); t < game.history.size(); t++) {
		low = std::min(low, game.history[t].gainDb);
		high = std::max(high, game.history[t].gainDb);
	}
	CHECK(high - low < 0.6f);
	CHECK(directionChanges(game, 5.0, 60.0, totalGain) <= 1);
	CHECK(directionChanges(game, 5.0, 60.0, levelGain) == 0);
	CHECK(game.maxOutputPeak <= static_cast<float>(dbToLinear(-0.9)));
}

TEST_CASE("regulator", down_faster_than_up)
{
	// Zu laut wird schnell leiser, zu leise nur langsam lauter
	Simulation loud(voiceFirst());
	Source &a = loud.add(Role::Game, gameNoise(20.0, -14.0f, 3));
	loud.run(20.0);
	Simulation quiet(voiceFirst());
	Source &b = quiet.add(Role::Game, gameNoise(20.0, -38.0f, 3));
	quiet.run(20.0);
	dump("laut", a, 0.5);
	dump("leise", b, 0.5);

	// Ab Aktivität: −12 dB bei 6 dB/s brauchen ca. 2 s, +12 dB bei 1,5 dB/s ca. 8 s
	CHECK(a.history[tickAt(4.0)].levelGain < -10.5f);
	CHECK(b.history[tickAt(4.0)].levelGain < 6.0f);
	CHECK(b.history[tickAt(4.0)].levelGain > 1.0f);
	CHECK_NEAR(b.history.back().levelGain, 12.0, 1.0);
}

TEST_CASE("regulator", deadzone_keeps_small_deviations)
{
	// Nur 0,7 LU daneben: nichts tun
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, gameNoise(20.0, -25.3f, 4));
	sim.run(20.0);
	dump("game", game);
	CHECK_NEAR(game.history.back().levelGain, 0.0, 1e-6);
}

TEST_CASE("regulator", ducking_while_speaking)
{
	// Stimme: 1 s Ruhe, dann 3 s sprechen / 4 s Pause im Wechsel; Spiel läuft durch
	Simulation sim(voiceFirst());
	Source &mic = sim.add(Role::Voice, voice(30.0, -18.0f, 3.0, 4.0, 1.0));
	Source &game = sim.add(Role::Game, gameNoise(30.0, -26.0f, 5));
	sim.run(30.0);

	// Während des Sprechens ist das Spiel 4 dB leiser
	float ducked = 0.0f;
	for (double t = 2.5; t < 3.8; t += kTick)
		ducked = std::min(ducked, game.history[tickAt(t)].duckGain);
	CHECK_NEAR(ducked, -4.0, 0.3);
	CHECK(game.history[tickAt(3.0)].ducking);
	// Die Stimme selbst wird nicht geduckt
	CHECK_NEAR(mic.history[tickAt(3.0)].duckGain, 0.0, 1e-6);

	// Ende des Sprechens finden und prüfen: erst halten, dann zurück
	size_t end = 0;
	for (size_t t = tickAt(2.0); t < game.history.size(); t++) {
		if (!game.history[t].ducking && game.history[t - 1].ducking) {
			end = t;
			break;
		}
	}
	CHECK(end > 0);
	if (end > 0) {
		// Spätestens 0,5 s nach dem letzten Wort noch geduckt (Haltezeit 0,8 s + Nachlauf der Erkennung)
		size_t lastSpeech = end;
		while (lastSpeech > 0 && !mic.history[lastSpeech].active)
			lastSpeech--;
		CHECK(end - lastSpeech >= tickAt(0.5));
		// 1,5 s nach dem Ende des Duckings ist das Spiel wieder zurück
		CHECK(game.history[end + tickAt(1.5)].duckGain > -0.5f);
	}
}

TEST_CASE("regulator", scream_guard_reacts_fast)
{
	// Normales Sprechen bei −18 LUFS, nach 12 s ein Schrei (+16 dB) für 2 s
	auto speech = voice(20.0, -18.0f, 0.0, 0.0, 0.0, 6);
	signals::gainRange(speech, kRate, 12.0, 14.0, 16.0);
	Simulation sim(voiceFirst());
	Source &mic = sim.add(Role::Voice, speech);
	sim.run(20.0);
	dump("mic", mic, 0.25);

	const float before = mic.history[tickAt(11.5)].gainDb;
	// Innerhalb von 300 ms nach Beginn des Schreis deutlich leiser
	float reduced = 0.0f;
	for (double t = 12.0; t <= 12.3; t += kTick)
		reduced = std::min(reduced, mic.history[tickAt(t)].guardGain);
	CHECK(reduced <= -6.0f);
	bool event = false;
	for (double t = 12.0; t <= 12.5; t += kTick)
		event = event || (mic.history[tickAt(t)].events & kEventGuard);
	CHECK(event);
	// Der Schrei verschiebt die gelernte Sprachlautheit kaum, danach ist alles wieder wie vorher
	CHECK_NEAR(mic.history[tickAt(19.5)].gainDb, before, 1.5);
	CHECK(mic.maxOutputPeak <= static_cast<float>(dbToLinear(-0.9)));
}

TEST_CASE("regulator", explosion_does_not_shift_balance)
{
	// Spiel bei −20 LUFS; nach 40 s eine Explosion (+15 dB, 1,5 s)
	auto noise = gameNoise(60.0, -20.0f, 8);
	signals::gainRange(noise, kRate, 40.0, 41.5, 15.0);
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, noise);
	sim.run(60.0);
	dump("game", game, 0.5);

	const ChannelState &before = game.history[tickAt(39.5)];
	const ChannelState &after = game.history[tickAt(50.0)];
	CHECK_NEAR(after.estimate, before.estimate, 1.0);
	CHECK_NEAR(after.levelGain, before.levelGain, 0.5);
	// Während der Explosion greift der Schreischutz
	float guard = 0.0f;
	for (double t = 40.0; t < 41.5; t += kTick)
		guard = std::min(guard, game.history[tickAt(t)].guardGain);
	CHECK(guard < -5.0f);
	// … und danach ist er wieder weg
	CHECK_NEAR(game.history[tickAt(50.0)].guardGain, 0.0, 0.1);
	CHECK(game.maxOutputPeak <= static_cast<float>(dbToLinear(-0.9)));
}

TEST_CASE("regulator", persistent_change_is_learned)
{
	// Das Spiel wird dauerhaft 8 dB lauter (z. B. anderes Spiel): nach einigen Sekunden nachgeführt
	auto noise = gameNoise(70.0, -24.0f, 9);
	signals::gainRange(noise, kRate, 30.0, 70.0, 8.0);
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, noise);
	sim.run(70.0);
	dump("game", game);
	CHECK_NEAR(game.history[tickAt(29.0)].levelGain, -2.0, 1.0);
	CHECK_NEAR(game.history.back().levelGain, -10.0, 1.2);
}

TEST_CASE("regulator", silence_never_raises_gain)
{
	// Spiel 20 s laut, dann 20 s Stille: in der Stille darf nichts hochgezogen werden
	auto noise = gameNoise(20.0, -20.0f, 10);
	noise.resize(signals::samples(kRate, 40.0), 0.0f);
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, noise);
	sim.run(40.0);
	const float atSilence = game.history[tickAt(20.5)].levelGain;
	for (size_t t = tickAt(20.5); t < game.history.size(); t++)
		CHECK(game.history[t].levelGain <= atSilence + 1e-4f);
	CHECK(!game.history.back().active);
}

TEST_CASE("regulator", competitive_keeps_dynamics)
{
	// Kein Ducking, kein Schreischutz – Spitzen fängt nur der Limiter
	auto noise = gameNoise(30.0, -20.0f, 12);
	signals::gainRange(noise, kRate, 20.0, 21.0, 18.0);
	Simulation sim(competitive());
	sim.add(Role::Voice, voice(30.0, -18.0f, 3.0, 3.0, 1.0));
	Source &game = sim.add(Role::Game, noise);
	sim.run(30.0);
	for (const ChannelState &state : game.history) {
		CHECK_NEAR(state.duckGain, 0.0, 1e-6);
		CHECK_NEAR(state.guardGain, 0.0, 1e-6);
	}
	CHECK(game.maxOutputPeak <= static_cast<float>(dbToLinear(-0.9)));
}

TEST_CASE("regulator", locked_source_is_untouched)
{
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, gameNoise(20.0, -16.0f, 13));
	game.channel.in.locked = true;
	game.channel.state.levelGain = -3.0f;
	game.channel.state.gainDb = -3.0f;
	sim.run(20.0);
	for (const ChannelState &state : game.history)
		CHECK_NEAR(state.gainDb, -3.0, 1e-6);
}

TEST_CASE("regulator", fader_is_taken_into_account)
{
	// Fader bei −6 dB: der Filter muss 6 dB weniger absenken, damit das Ergebnis nach dem Fader stimmt
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, gameNoise(30.0, -16.0f, 14));
	game.channel.in.faderDb = -6.0f;
	sim.run(30.0);
	CHECK_NEAR(game.history.back().levelGain, -4.0, 1.0);
}

TEST_CASE("regulator", reference_shifts_targets)
{
	// Ziel-Lautheit der Stimme −18 statt −16: alle Ziele 2 LU tiefer
	Regulator regulator;
	regulator.setPreset(voiceFirst());
	RegulatorConfig config;
	config.referenceLufs = -18.0f;
	regulator.setConfig(config);
	CHECK_NEAR(regulator.target(Role::Voice), -18.0, 1e-6);
	CHECK_NEAR(regulator.target(Role::Game), -28.0, 1e-6);
	CHECK(isSilent(regulator.target(Role::Ignore)));
}

TEST_CASE("regulator", limits_are_respected)
{
	// Sehr leise Quelle: höchstens +12 dB
	Simulation sim(voiceFirst());
	Source &game = sim.add(Role::Game, gameNoise(30.0, -45.0f, 15));
	game.channel.in.maxGainDb = 6.0f;
	sim.run(30.0);
	CHECK(game.history.back().gainDb <= 6.0f + 1e-4f);
}
