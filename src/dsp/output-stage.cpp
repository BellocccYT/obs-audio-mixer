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

#include "output-stage.hpp"

#include "types.hpp"

#include <algorithm>
#include <cmath>

namespace am::dsp {

void OutputStage::configure(const Config &config)
{
	config_ = config;
	if (config_.sampleRate <= 0.0)
		config_.sampleRate = 48000.0;
	const double rate = config_.sampleRate;
	window_ = std::max<size_t>(1, static_cast<size_t>(std::lround(config_.lookaheadMs / 1000.0 * rate)));
	latency_ = static_cast<size_t>(Oversampler4x::kDelay) + window_ - 1;
	delaySize_ = latency_ + 1;
	channels_.assign(static_cast<size_t>(std::max(config_.channels, 1)), Channel());
	for (Channel &channel : channels_)
		channel.delay.assign(delaySize_, 0.0f);
	minValues_.assign(window_, 1.0f);
	minIndices_.assign(window_, 0);
	envelope_.assign(window_, 1.0f);
	smoothing_ = 1.0 - std::exp(-1.0 / (std::max(config_.smoothingMs, 0.1f) / 1000.0 * rate));
	release_ = 1.0 - std::exp(-1.0 / (std::max(config_.releaseMs, 1.0f) / 1000.0 * rate));
	reset();
}

void OutputStage::reset()
{
	for (Channel &channel : channels_) {
		std::fill(channel.delay.begin(), channel.delay.end(), 0.0f);
		channel.history.reset();
		channel.countdown = 0;
	}
	delayHead_ = 0;
	sampleIndex_ = 0;
	gain_ = 1.0;
	resetLimiter();
}

void OutputStage::resetLimiter()
{
	minHead_ = minCount_ = 0;
	std::fill(envelope_.begin(), envelope_.end(), 1.0f);
	envelopeHead_ = 0;
	envelopeSum_ = static_cast<double>(window_);
	released_ = 1.0f;
	minGain_ = 1.0f;
	for (Channel &channel : channels_)
		channel.countdown = 0;
	limiterIdle_ = true;
}

void OutputStage::process(float *const *data, size_t frames, float gainDb, bool limiter, float ceilingDb)
{
	const double target = dbToLinear(std::clamp(gainDb, -90.0f, 40.0f));
	const size_t channels = channels_.size();
	const size_t readOffset = delaySize_ - latency_;
	minGain_ = 1.0f;

	if (!limiter) {
		if (!limiterIdle_)
			resetLimiter();
		// Nur Verstärkung; die Verzögerung bleibt, damit sich die Latenz beim Umschalten nicht ändert
		for (size_t i = 0; i < frames; i++) {
			gain_ += (target - gain_) * smoothing_;
			const float g = static_cast<float>(gain_);
			const size_t read = (delayHead_ + readOffset) % delaySize_;
			for (size_t c = 0; c < channels; c++) {
				if (!data[c])
					continue;
				Channel &channel = channels_[c];
				const float v = data[c][i] * g;
				channel.history.push(v);
				channel.delay[delayHead_] = v;
				data[c][i] = channel.delay[read];
			}
			delayHead_ = delayHead_ + 1 == delaySize_ ? 0 : delayHead_ + 1;
			sampleIndex_++;
		}
		return;
	}

	const float ceiling = static_cast<float>(dbToLinear(ceilingDb));
	const float gate = ceiling * 0.5f; // nur in der Nähe der Obergrenze überabtasten
	const float release = static_cast<float>(release_);
	for (size_t i = 0; i < frames; i++) {
		gain_ += (target - gain_) * smoothing_;
		const float g = static_cast<float>(gain_);

		// Spitzenwert um das Sample, das kDelay Schritte zurückliegt (inklusive Zwischenwerte)
		float peak = 0.0f;
		for (size_t c = 0; c < channels; c++) {
			if (!data[c])
				continue;
			Channel &channel = channels_[c];
			const float v = data[c][i] * g;
			channel.history.push(v);
			channel.delay[delayHead_] = v;
			if (std::fabs(v) >= gate)
				channel.countdown = SampleHistory::kSize;
			float p = std::fabs(channel.history.at(Oversampler4x::kDelay));
			if (channel.countdown > 0) {
				channel.countdown--;
				float between[3];
				oversampler_.interpolate(channel.history.window(), between);
				p = std::max({p, std::fabs(between[0]), std::fabs(between[1]), std::fabs(between[2])});
			}
			peak = std::max(peak, p);
		}
		const float required = peak > ceiling ? ceiling / peak : 1.0f;

		// Gleitendes Minimum über das Lookahead-Fenster
		while (minCount_ > 0 && minIndices_[minHead_] + window_ <= sampleIndex_) {
			minHead_ = minHead_ + 1 == window_ ? 0 : minHead_ + 1;
			minCount_--;
		}
		if (required < 1.0f) {
			while (minCount_ > 0 && minValues_[(minHead_ + minCount_ - 1) % window_] >= required)
				minCount_--;
			const size_t back = (minHead_ + minCount_) % window_;
			minValues_[back] = required;
			minIndices_[back] = sampleIndex_;
			minCount_++;
		}
		const float held = minCount_ > 0 ? minValues_[minHead_] : 1.0f;

		// Release: langsam zurück Richtung 1, aber nie über das gehaltene Minimum
		released_ = std::min(held, released_ + (1.0f - released_) * release);
		if (released_ > 0.9999f && held >= 1.0f)
			released_ = 1.0f; // sonst nähert sich der Wert in float nur endlos an 1 an

		// Gleitender Mittelwert über dieselbe Länge
		envelopeSum_ += static_cast<double>(released_) - static_cast<double>(envelope_[envelopeHead_]);
		envelope_[envelopeHead_] = released_;
		envelopeHead_ = envelopeHead_ + 1 == window_ ? 0 : envelopeHead_ + 1;
		const float limiterGain =
			std::min(1.0f, static_cast<float>(envelopeSum_ / static_cast<double>(window_)));
		minGain_ = std::min(minGain_, limiterGain);

		const size_t read = (delayHead_ + readOffset) % delaySize_;
		for (size_t c = 0; c < channels; c++) {
			if (data[c])
				data[c][i] = channels_[c].delay[read] * limiterGain;
		}
		delayHead_ = delayHead_ + 1 == delaySize_ ? 0 : delayHead_ + 1;
		sampleIndex_++;
	}

	limiterIdle_ = minCount_ == 0 && released_ >= 1.0f && minGain_ >= 1.0f;
	if (limiterIdle_) {
		// Rundungsfehler der laufenden Summe nicht anwachsen lassen
		envelopeSum_ = static_cast<double>(window_);
	}
}

float OutputStage::gainDb() const
{
	return linearToDb(gain_);
}

float OutputStage::gainReductionDb() const
{
	return minGain_ >= 1.0f ? 0.0f : linearToDb(minGain_);
}

} // namespace am::dsp
