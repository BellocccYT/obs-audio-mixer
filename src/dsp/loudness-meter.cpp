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

#include "loudness-meter.hpp"

#include <algorithm>

namespace am::dsp {

namespace {

constexpr float kHistogramMin = -70.0f; // absolutes Gate nach BS.1770
constexpr float kHistogramStep = 0.1f;

int histogramBin(float lufs)
{
	const int bin = static_cast<int>(std::floor((lufs - kHistogramMin) / kHistogramStep));
	return std::clamp(bin, 0, 799);
}

float binCenter(int bin)
{
	return kHistogramMin + (static_cast<float>(bin) + 0.5f) * kHistogramStep;
}

bool isZero(const float *in, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (in[i] != 0.0f)
			return false;
	}
	return true;
}

// K-Filter über n Samples eines Kanals, liefert die Summe der Quadrate. Die Filter werden lokal kopiert,
// damit der Compiler ihren Zustand in Registern halten kann.
double filterChannel(Biquad &shelfState, Biquad &highpassState, const float *in, size_t n)
{
	Biquad shelf = shelfState;
	Biquad highpass = highpassState;
	double sum = 0.0;
	for (size_t i = 0; i < n; i++) {
		const double y = highpass.process(shelf.process(in[i]));
		sum += y * y;
	}
	shelfState = shelf;
	highpassState = highpass;
	return sum;
}

// Wie filterChannel, aber zwei Kanäle verschränkt: zwei unabhängige Rechenketten nutzen die CPU besser
void filterStereo(Biquad *shelfState, Biquad *highpassState, const float *left, const float *right, size_t n,
		  double &sumLeft, double &sumRight)
{
	Biquad shelfL = shelfState[0], shelfR = shelfState[1];
	Biquad highpassL = highpassState[0], highpassR = highpassState[1];
	double l = 0.0, r = 0.0;
	for (size_t i = 0; i < n; i++) {
		const double yl = highpassL.process(shelfL.process(left[i]));
		const double yr = highpassR.process(shelfR.process(right[i]));
		l += yl * yl;
		r += yr * yr;
	}
	shelfState[0] = shelfL;
	shelfState[1] = shelfR;
	highpassState[0] = highpassL;
	highpassState[1] = highpassR;
	sumLeft = l;
	sumRight = r;
}

} // namespace

KWeighting kWeighting(double sampleRate)
{
	// Analoge Vorbilder der beiden Filter, per bilinearer Transformation auf die Abtastrate gebracht
	// (dieselbe Herleitung wie in libebur128)
	KWeighting k;

	double f0 = 1681.974450955533;
	const double gain = 3.999843853973347;
	double q = 0.7071752369554196;
	double K = std::tan(kPi * f0 / sampleRate);
	const double vh = std::pow(10.0, gain / 20.0);
	const double vb = std::pow(vh, 0.4996667741545416);
	double a0 = 1.0 + K / q + K * K;
	k.shelf.b0 = (vh + vb * K / q + K * K) / a0;
	k.shelf.b1 = 2.0 * (K * K - vh) / a0;
	k.shelf.b2 = (vh - vb * K / q + K * K) / a0;
	k.shelf.a1 = 2.0 * (K * K - 1.0) / a0;
	k.shelf.a2 = (1.0 - K / q + K * K) / a0;

	f0 = 38.13547087602444;
	q = 0.5003270373238773;
	K = std::tan(kPi * f0 / sampleRate);
	a0 = 1.0 + K / q + K * K;
	k.highpass.b0 = 1.0;
	k.highpass.b1 = -2.0;
	k.highpass.b2 = 1.0;
	k.highpass.a1 = 2.0 * (K * K - 1.0) / a0;
	k.highpass.a2 = (1.0 - K / q + K * K) / a0;
	return k;
}

std::vector<float> channelWeights(int channels)
{
	constexpr float s = 1.41f; // Surround-Kanäle zählen etwas mehr
	switch (channels) {
	case 1:
		return {1.0f};
	case 2:
		return {1.0f, 1.0f};
	case 3: // 2.1: FL FR LFE
		return {1.0f, 1.0f, 0.0f};
	case 4: // 4.0: FL FR FC RC
		return {1.0f, 1.0f, 1.0f, s};
	case 5: // 4.1: FL FR FC LFE RC
		return {1.0f, 1.0f, 1.0f, 0.0f, s};
	case 6: // 5.1: FL FR FC LFE RL RR
		return {1.0f, 1.0f, 1.0f, 0.0f, s, s};
	case 8: // 7.1: FL FR FC LFE RL RR SL SR
		return {1.0f, 1.0f, 1.0f, 0.0f, s, s, s, s};
	default:
		return std::vector<float>(static_cast<size_t>(std::max(channels, 0)), 1.0f);
	}
}

void LoudnessMeter::Histogram::clear()
{
	counts.fill(0);
	powers.fill(0.0);
}

void LoudnessMeter::Histogram::add(double power)
{
	const float lufs = powerToLufs(power);
	if (lufs < kHistogramMin)
		return;
	const int bin = histogramBin(lufs);
	counts[static_cast<size_t>(bin)]++;
	powers[static_cast<size_t>(bin)] += power;
}

void LoudnessMeter::configure(double sampleRate, const std::vector<float> &weights)
{
	sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
	weights_ = weights;
	const KWeighting k = kWeighting(sampleRate_);
	shelf_.assign(weights_.size(), Biquad());
	highpass_.assign(weights_.size(), Biquad());
	for (size_t c = 0; c < weights_.size(); c++) {
		shelf_[c].setCoeffs(k.shelf);
		highpass_[c].setCoeffs(k.highpass);
	}
	frameLength_ = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate_ / 100.0)));
	blockLength_ = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate_ / 10.0)));
	reset();
}

void LoudnessMeter::reset()
{
	for (Biquad &filter : shelf_)
		filter.reset();
	for (Biquad &filter : highpass_)
		filter.reset();
	frameRemaining_ = frameLength_;
	blockRemaining_ = blockLength_;
	frameSum_ = 0.0;
	blockSum_ = 0.0;
	blockPowers_.fill(0.0);
	blockHead_ = 0;
	blocks_ = 0;
	lastBlock_ = momentary_ = shortTerm_ = kSilence;
	gatingBlocks_.clear();
	shortTermValues_.clear();
}

void LoudnessMeter::process(const float *const *data, size_t frames)
{
	const size_t channels = weights_.size();
	size_t pos = 0;
	while (pos < frames) {
		// Bis zur nächsten Grenze eines 10-ms-Abschnitts oder 100-ms-Blocks am Stück rechnen
		const size_t n = std::min({frames - pos, frameRemaining_, blockRemaining_});
		double weighted = 0.0;
		if (channels == 2 && data[0] && data[1] && weights_[0] != 0.0f && weights_[1] != 0.0f) {
			const float *left = data[0] + pos;
			const float *right = data[1] + pos;
			const bool silent = shelf_[0].idle() && highpass_[0].idle() && shelf_[1].idle() &&
					    highpass_[1].idle() && isZero(left, n) && isZero(right, n);
			if (!silent) {
				double sumLeft, sumRight;
				filterStereo(shelf_.data(), highpass_.data(), left, right, n, sumLeft, sumRight);
				weighted = static_cast<double>(weights_[0]) * sumLeft +
					   static_cast<double>(weights_[1]) * sumRight;
			}
		} else {
			for (size_t c = 0; c < channels; c++) {
				if (weights_[c] == 0.0f || !data[c])
					continue;
				const float *in = data[c] + pos;
				// Digitale Stille (z. B. ungenutzte Spur): nichts zu rechnen
				if (shelf_[c].idle() && highpass_[c].idle() && isZero(in, n))
					continue;
				weighted += static_cast<double>(weights_[c]) *
					    filterChannel(shelf_[c], highpass_[c], in, n);
			}
		}
		frameSum_ += weighted;
		blockSum_ += weighted;
		pos += n;
		frameRemaining_ -= n;
		blockRemaining_ -= n;
		if (frameRemaining_ == 0)
			finishFrame();
		if (blockRemaining_ == 0)
			finishBlock();
	}
}

void LoudnessMeter::finishFrame()
{
	const double power = frameSum_ / static_cast<double>(frameLength_);
	frameSum_ = 0.0;
	frameRemaining_ = frameLength_;
	if (listener_)
		listener_->onFrame(power);
}

void LoudnessMeter::finishBlock()
{
	const double power = blockSum_ / static_cast<double>(blockLength_);
	blockSum_ = 0.0;
	blockRemaining_ = blockLength_;

	blockPowers_[static_cast<size_t>(blockHead_)] = power;
	blockHead_ = (blockHead_ + 1) % kShortTermBlocks;
	blocks_++;

	double momentary = 0.0;
	double shortTerm = 0.0;
	for (int i = 0; i < kShortTermBlocks; i++) {
		const int index = (blockHead_ - 1 - i + kShortTermBlocks) % kShortTermBlocks;
		const double value = blockPowers_[static_cast<size_t>(index)];
		if (i < kMomentaryBlocks)
			momentary += value;
		shortTerm += value;
	}
	momentary /= kMomentaryBlocks;
	shortTerm /= kShortTermBlocks;

	lastBlock_ = powerToLufs(power);
	momentary_ = powerToLufs(momentary);
	shortTerm_ = powerToLufs(shortTerm);

	// Gating-Blöcke (400 ms, 75 % Überlappung) und Short-term-Werte (alle 100 ms) für die Statistik
	if (blocks_ >= kMomentaryBlocks)
		gatingBlocks_.add(momentary);
	if (blocks_ >= kShortTermBlocks)
		shortTermValues_.add(shortTerm);

	for (Biquad &filter : shelf_)
		filter.flushDenormals();
	for (Biquad &filter : highpass_)
		filter.flushDenormals();

	if (listener_)
		listener_->onBlock(power);
}

float LoudnessMeter::integrated() const
{
	// Absolutes Gate −70 LUFS steckt schon im Histogramm, danach relatives Gate −10 LU
	double sum = 0.0;
	uint64_t count = 0;
	for (int b = 0; b < kHistogramBins; b++) {
		sum += gatingBlocks_.powers[static_cast<size_t>(b)];
		count += gatingBlocks_.counts[static_cast<size_t>(b)];
	}
	if (count == 0)
		return kSilence;
	const float relativeGate = powerToLufs(sum / static_cast<double>(count)) - 10.0f;
	sum = 0.0;
	count = 0;
	for (int b = 0; b < kHistogramBins; b++) {
		if (binCenter(b) < relativeGate)
			continue;
		sum += gatingBlocks_.powers[static_cast<size_t>(b)];
		count += gatingBlocks_.counts[static_cast<size_t>(b)];
	}
	return count ? powerToLufs(sum / static_cast<double>(count)) : kSilence;
}

float LoudnessMeter::loudnessRange() const
{
	// EBU Tech 3342: Short-term-Werte, absolutes Gate −70 LUFS, relatives Gate −20 LU,
	// dann Abstand zwischen 10. und 95. Perzentil
	double sum = 0.0;
	uint64_t count = 0;
	for (int b = 0; b < kHistogramBins; b++) {
		sum += shortTermValues_.powers[static_cast<size_t>(b)];
		count += shortTermValues_.counts[static_cast<size_t>(b)];
	}
	if (count == 0)
		return 0.0f;
	const float relativeGate = powerToLufs(sum / static_cast<double>(count)) - 20.0f;
	int first = 0;
	while (first < kHistogramBins && binCenter(first) < relativeGate)
		first++;
	uint64_t gated = 0;
	for (int b = first; b < kHistogramBins; b++)
		gated += shortTermValues_.counts[static_cast<size_t>(b)];
	if (gated == 0)
		return 0.0f;

	auto percentile = [&](double fraction) {
		const uint64_t wanted =
			std::max<uint64_t>(1, static_cast<uint64_t>(std::ceil(fraction * static_cast<double>(gated))));
		uint64_t seen = 0;
		for (int b = first; b < kHistogramBins; b++) {
			seen += shortTermValues_.counts[static_cast<size_t>(b)];
			if (seen >= wanted)
				return binCenter(b);
		}
		return binCenter(kHistogramBins - 1);
	};
	return percentile(0.95) - percentile(0.10);
}

} // namespace am::dsp
