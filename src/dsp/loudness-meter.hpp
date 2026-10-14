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

#include "biquad.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace am::dsp {

// Die beiden Filter der K-Gewichtung (Höhenanhebung und Hochpass) für eine beliebige Abtastrate.
// Bei 48 kHz entstehen genau die Koeffizienten aus ITU-R BS.1770.
struct KWeighting {
	BiquadCoeffs shelf;
	BiquadCoeffs highpass;
};
KWeighting kWeighting(double sampleRate);

// Kanalgewichte nach BS.1770 für die Lautsprecher-Anordnungen von OBS (Reihenfolge wie in libobs):
// vorne 1,0, Surround 1,41, LFE 0
std::vector<float> channelWeights(int channels);

// Lautheitsmessung nach ITU-R BS.1770-4 / EBU R128: Momentary (400 ms), Short-term (3 s),
// Integrated (mit Gating) und Loudness Range (EBU Tech 3342). Gemessen wird in 100-ms-Blöcken.
// configure() legt alle Puffer an; process() ist danach echtzeitfähig (keine Speicheranforderung).
class LoudnessMeter {
public:
	// Bekommt die K-gewichtete mittlere Leistung jedes 10-ms-Abschnitts und jedes 100-ms-Blocks
	class Listener {
	public:
		virtual ~Listener() = default;
		virtual void onFrame(double power) = 0;
		virtual void onBlock(double power) = 0;
	};

	void configure(double sampleRate, const std::vector<float> &weights);
	void setListener(Listener *listener) { listener_ = listener; }
	void reset();

	// Planare float-Samples, ein Zeiger pro Kanal
	void process(const float *const *data, size_t frames);

	float lastBlock() const { return lastBlock_; }            // letzte 100 ms
	float momentary() const { return momentary_; }            // letzte 400 ms
	float shortTerm() const { return shortTerm_; }            // letzte 3 s
	float integrated() const;                                 // seit reset(), mit absolutem und relativem Gate
	float loudnessRange() const;                              // in LU, seit reset()
	uint64_t blocks() const { return blocks_; }               // fertige 100-ms-Blöcke seit reset()
	size_t blockRemaining() const { return blockRemaining_; } // Samples bis zum Ende des aktuellen Blocks

	double sampleRate() const { return sampleRate_; }
	int channels() const { return static_cast<int>(weights_.size()); }

private:
	static constexpr int kShortTermBlocks = 30;
	static constexpr int kMomentaryBlocks = 4;
	static constexpr int kHistogramBins = 800; // 0,1 LU von −70 bis +10 LUFS

	struct Histogram {
		std::array<uint32_t, kHistogramBins> counts{};
		std::array<double, kHistogramBins> powers{};
		void clear();
		void add(double power);
	};

	void finishFrame();
	void finishBlock();

	double sampleRate_ = 48000.0;
	std::vector<float> weights_;
	std::vector<Biquad> shelf_;
	std::vector<Biquad> highpass_;
	Listener *listener_ = nullptr;

	size_t frameLength_ = 480;
	size_t blockLength_ = 4800;
	size_t frameRemaining_ = 480;
	size_t blockRemaining_ = 4800;
	double frameSum_ = 0.0;
	double blockSum_ = 0.0;

	std::array<double, kShortTermBlocks> blockPowers_{};
	int blockHead_ = 0;
	uint64_t blocks_ = 0;

	float lastBlock_ = kSilence;
	float momentary_ = kSilence;
	float shortTerm_ = kSilence;

	Histogram gatingBlocks_;    // 400-ms-Blöcke für Integrated
	Histogram shortTermValues_; // Short-term-Werte für die Loudness Range
};

} // namespace am::dsp
