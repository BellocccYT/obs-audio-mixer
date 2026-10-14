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

#include "analyzer.hpp"

#include <algorithm>

namespace am::dsp {

namespace {

constexpr int kMaxChannels = 8;           // wie MAX_AUDIO_CHANNELS in libobs
constexpr float kClipLevel = 0.9999f;     // ab hier zählt ein Sample als übersteuert
constexpr float kSpeechBlockRatio = 0.5f; // Block zählt als Sprache, wenn mindestens die Hälfte Sprache ist

} // namespace

void Analyzer::configure(double sampleRate, int channels, const Config &config)
{
	config_ = config;
	channels = std::clamp(channels, 1, kMaxChannels);
	meter_.configure(sampleRate, channelWeights(channels));
	meter_.setListener(this);
	truePeak_.configure(channels, config_.truePeakGate);
	vad_.configure(100.0, config_.vadConfig);
	reset();
}

void Analyzer::reset()
{
	meter_.reset();
	truePeak_.reset();
	vad_.reset();
	snapshot_ = AnalyzerSnapshot();
	newBlock_ = false;
	blockTruePeak_ = blockSamplePeak_ = 0.0f;
	truePeaks_.fill(0.0f);
	samplePeaks_.fill(0.0f);
	peakHead_ = 0;
	framesInBlock_ = speechFrames_ = 0;
	speechPowers_.fill(0.0);
	speechHead_ = speechCount_ = 0;
}

void Analyzer::process(const float *const *data, size_t frames)
{
	const int channels = meter_.channels();
	const float *chunk[kMaxChannels] = {};
	size_t pos = 0;
	while (pos < frames) {
		// In Stücken bis zum Blockende arbeiten, damit Spitzen dem richtigen Block zugeordnet werden
		const size_t n = std::min(frames - pos, meter_.blockRemaining());
		for (int c = 0; c < channels; c++)
			chunk[c] = data[c] ? data[c] + pos : nullptr;

		for (int c = 0; c < channels; c++) {
			if (!chunk[c])
				continue;
			const float *in = chunk[c];
			float peak = blockSamplePeak_;
			uint64_t clipped = 0;
			for (size_t i = 0; i < n; i++) {
				const float magnitude = std::fabs(in[i]);
				peak = std::max(peak, magnitude);
				clipped += magnitude >= kClipLevel ? 1 : 0;
			}
			blockSamplePeak_ = peak;
			snapshot_.clipped += clipped;
		}
		blockTruePeak_ = std::max(blockTruePeak_, truePeak_.process(chunk, n));

		meter_.process(chunk, n); // ruft am Blockende onBlock() auf
		pos += n;
	}
}

void Analyzer::onFrame(double power)
{
	framesInBlock_++;
	if (!config_.vad)
		return;
	vad_.processFrame(power);
	if (vad_.speaking())
		speechFrames_++;
}

void Analyzer::onBlock(double power)
{
	truePeaks_[static_cast<size_t>(peakHead_)] = blockTruePeak_;
	samplePeaks_[static_cast<size_t>(peakHead_)] = blockSamplePeak_;
	peakHead_ = (peakHead_ + 1) % kPeakBlocks;
	blockTruePeak_ = blockSamplePeak_ = 0.0f;

	const float ratio = framesInBlock_ ? static_cast<float>(speechFrames_) / static_cast<float>(framesInBlock_)
					   : 0.0f;
	framesInBlock_ = speechFrames_ = 0;
	const bool speechBlock = config_.vad && ratio >= kSpeechBlockRatio;
	if (speechBlock) {
		speechPowers_[static_cast<size_t>(speechHead_)] = power;
		speechHead_ = (speechHead_ + 1) % kSpeechBlocks;
		speechCount_ = std::min(speechCount_ + 1, kSpeechBlocks);
	}
	double speech = 0.0;
	for (int i = 0; i < speechCount_; i++)
		speech += speechPowers_[static_cast<size_t>(i)];

	snapshot_.blocks = meter_.blocks();
	snapshot_.block = meter_.lastBlock();
	snapshot_.momentary = meter_.momentary();
	snapshot_.shortTerm = meter_.shortTerm();
	snapshot_.speech = speechCount_ ? powerToLufs(speech / speechCount_) : kSilence;
	snapshot_.speechBlock = speechBlock ? powerToLufs(power) : kSilence;
	snapshot_.noiseFloor = config_.vad ? vad_.noiseFloor() : kSilence;
	snapshot_.truePeak = linearToDb(*std::max_element(truePeaks_.begin(), truePeaks_.end()));
	snapshot_.samplePeak = linearToDb(*std::max_element(samplePeaks_.begin(), samplePeaks_.end()));
	snapshot_.speaking = config_.vad && vad_.speaking();
	snapshot_.speechRatio = ratio;
	newBlock_ = true;
}

} // namespace am::dsp
