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

#include "loudness-meter.hpp"
#include "true-peak.hpp"
#include "types.hpp"
#include "vad.hpp"

#include <array>
#include <cstdint>

namespace am::dsp {

// Messwerte einer Quelle oder Tonspur, alle 100 ms aktualisiert
struct AnalyzerSnapshot {
	uint64_t blocks = 0;          // fertige 100-ms-Blöcke seit dem Start (zeigt, ob Daten nachkommen)
	float block = kSilence;       // LUFS der letzten 100 ms
	float momentary = kSilence;   // LUFS, 400 ms
	float shortTerm = kSilence;   // LUFS, 3 s
	float speech = kSilence;      // LUFS nur über Sprachabschnitte (die letzten 3 s Sprache)
	float speechBlock = kSilence; // LUFS des letzten Blocks, wenn er Sprache war (sonst kSilence)
	float noiseFloor = kSilence;  // Rauschboden der Spracherkennung (LUFS)
	float truePeak = kSilence;    // dBTP, höchster Wert der letzten 500 ms
	float samplePeak = kSilence;  // dBFS, höchster Wert der letzten 500 ms
	uint64_t clipped = 0;         // laufende Zahl übersteuerter Samples (Betrag ≥ 0 dBFS)
	bool speaking = false;
	float speechRatio = 0.0f; // Anteil Sprache im letzten Block (0–1)
};

// Fasst Lautheit, True Peak, Übersteuerung und Spracherkennung für einen Audiostrom zusammen.
// configure() legt alle Puffer an; process() ist danach echtzeitfähig.
class Analyzer : private LoudnessMeter::Listener {
public:
	struct Config {
		bool vad = true;
		float truePeakGate = 0.25f; // erst ab −12 dBFS überabtasten (spart Rechenzeit)
		VadConfig vadConfig;
	};

	Analyzer() = default;
	Analyzer(const Analyzer &) = delete;
	Analyzer &operator=(const Analyzer &) = delete;

	void configure(double sampleRate, int channels, const Config &config);
	void configure(double sampleRate, int channels) { configure(sampleRate, channels, Config()); }
	void reset();

	void process(const float *const *data, size_t frames);

	// Aktuelle Werte; nach jedem fertigen Block aktualisiert
	const AnalyzerSnapshot &snapshot() const { return snapshot_; }
	// true, wenn seit dem letzten Aufruf ein neuer Block fertig wurde
	bool takeNewBlock()
	{
		const bool fresh = newBlock_;
		newBlock_ = false;
		return fresh;
	}

	LoudnessMeter &meter() { return meter_; }
	const LoudnessMeter &meter() const { return meter_; }

private:
	static constexpr int kPeakBlocks = 5;
	static constexpr int kSpeechBlocks = 30;

	void onFrame(double power) override;
	void onBlock(double power) override;

	Config config_;
	LoudnessMeter meter_;
	TruePeakMeter truePeak_;
	VoiceActivityDetector vad_;
	AnalyzerSnapshot snapshot_;
	bool newBlock_ = false;

	float blockTruePeak_ = 0.0f;
	float blockSamplePeak_ = 0.0f;
	std::array<float, kPeakBlocks> truePeaks_{};
	std::array<float, kPeakBlocks> samplePeaks_{};
	int peakHead_ = 0;

	int framesInBlock_ = 0;
	int speechFrames_ = 0;
	std::array<double, kSpeechBlocks> speechPowers_{};
	int speechHead_ = 0;
	int speechCount_ = 0;
};

} // namespace am::dsp
