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

// Künstliche Testsignale: Sinus, Rauschen, sprachähnliche Silbenfolgen

#include <dsp/biquad.hpp>
#include <dsp/loudness-meter.hpp>
#include <dsp/types.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace signals {

using am::dsp::kPi;

// Reproduzierbarer Zufall (xorshift32)
class Random {
public:
	explicit Random(uint32_t seed) : state_(seed ? seed : 1u) {}
	uint32_t next()
	{
		state_ ^= state_ << 13;
		state_ ^= state_ >> 17;
		state_ ^= state_ << 5;
		return state_;
	}
	float uniform() { return static_cast<float>(next() >> 8) / 16777216.0f * 2.0f - 1.0f; } // [-1, 1)
	float between(float low, float high) { return low + (uniform() * 0.5f + 0.5f) * (high - low); }

private:
	uint32_t state_;
};

inline size_t samples(double rate, double seconds)
{
	return static_cast<size_t>(std::llround(rate * seconds));
}

// Sinus mit Spitzenwert peakDb (dBFS)
inline std::vector<float> sine(double rate, double frequency, double seconds, double peakDb, double phaseDeg = 0.0)
{
	std::vector<float> out(samples(rate, seconds));
	const double amplitude = std::pow(10.0, peakDb / 20.0);
	const double phase = phaseDeg * kPi / 180.0;
	for (size_t i = 0; i < out.size(); i++)
		out[i] = static_cast<float>(amplitude *
					    std::sin(2.0 * kPi * frequency * static_cast<double>(i) / rate + phase));
	return out;
}

inline void append(std::vector<float> &target, const std::vector<float> &part)
{
	target.insert(target.end(), part.begin(), part.end());
}

// Folge von Sinus-Abschnitten mit wechselndem Pegel; die Phase läuft durch
inline std::vector<float> steppedSine(double rate, double frequency,
				      const std::vector<std::pair<double, double>> &steps)
{
	std::vector<float> out;
	size_t n = 0;
	for (const auto &[seconds, peakDb] : steps) {
		const double amplitude = std::pow(10.0, peakDb / 20.0);
		const size_t count = samples(rate, seconds);
		for (size_t i = 0; i < count; i++, n++)
			out.push_back(static_cast<float>(
				amplitude * std::sin(2.0 * kPi * frequency * static_cast<double>(n) / rate)));
	}
	return out;
}

// Rosa Rauschen (Filter nach Paul Kellet), ungefähr wie breitbandiger Spielton
inline std::vector<float> pinkNoise(double rate, double seconds, uint32_t seed)
{
	Random random(seed);
	std::vector<float> out(samples(rate, seconds));
	double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	for (float &sample : out) {
		const double white = random.uniform();
		b0 = 0.99886 * b0 + white * 0.0555179;
		b1 = 0.99332 * b1 + white * 0.0750759;
		b2 = 0.96900 * b2 + white * 0.1538520;
		b3 = 0.86650 * b3 + white * 0.3104856;
		b4 = 0.55000 * b4 + white * 0.5329522;
		b5 = -0.7616 * b5 - white * 0.0168980;
		sample = static_cast<float>((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11);
		b6 = white * 0.115926;
	}
	return out;
}

// Sprachähnliches Signal: bandbegrenztes Rauschen in Silben (ca. 4–6 pro Sekunde), Wörter mit kurzen
// Pausen, nach talkSeconds Sprechen folgt pauseSeconds Stille. talkSeconds = 0: durchgehend.
inline std::vector<float> speech(double rate, double seconds, uint32_t seed, double talkSeconds = 0.0,
				 double pauseSeconds = 0.0, double startPause = 0.0)
{
	Random random(seed);
	std::vector<float> out(samples(rate, seconds), 0.0f);
	am::dsp::Biquad highpass, lowpass;
	{
		// Bandpass ca. 250 Hz – 3 kHz (Butterworth, bilinear)
		auto design = [rate](double f, bool high) {
			const double k = std::tan(kPi * f / rate);
			const double q = 0.7071;
			const double norm = 1.0 / (1.0 + k / q + k * k);
			am::dsp::BiquadCoeffs c;
			if (high) {
				c.b0 = norm;
				c.b1 = -2.0 * norm;
				c.b2 = norm;
			} else {
				c.b0 = k * k * norm;
				c.b1 = 2.0 * c.b0;
				c.b2 = c.b0;
			}
			c.a1 = 2.0 * (k * k - 1.0) * norm;
			c.a2 = (1.0 - k / q + k * k) * norm;
			return c;
		};
		highpass.setCoeffs(design(250.0, true));
		lowpass.setCoeffs(design(3000.0, false));
	}
	size_t pos = samples(rate, startPause);
	double talked = 0.0;
	while (pos < out.size()) {
		// Ein Wort aus 1–4 Silben
		const int syllables = 1 + static_cast<int>(random.next() % 4);
		for (int s = 0; s < syllables && pos < out.size(); s++) {
			const size_t length = samples(rate, random.between(0.12f, 0.22f));
			const float loudness = random.between(0.5f, 1.0f);
			for (size_t i = 0; i < length && pos < out.size(); i++, pos++) {
				const double env = std::sin(kPi * static_cast<double>(i) / static_cast<double>(length));
				const double carrier = lowpass.process(highpass.process(random.uniform()));
				out[pos] = static_cast<float>(carrier * env * loudness);
			}
			pos += samples(rate, random.between(0.03f, 0.08f));
			talked += static_cast<double>(length) / rate;
		}
		pos += samples(rate, random.between(0.10f, 0.25f));
		if (talkSeconds > 0.0 && talked >= talkSeconds) {
			pos += samples(rate, pauseSeconds);
			talked = 0.0;
		}
	}
	return out;
}

// Lautheit eines Mono-Signals, das auf zwei Kanäle verteilt wird (wie eine Stereoquelle in OBS)
inline float stereoLoudness(double rate, const std::vector<float> &mono)
{
	am::dsp::LoudnessMeter meter;
	meter.configure(rate, am::dsp::channelWeights(2));
	const float *data[2] = {mono.data(), mono.data()};
	meter.process(data, mono.size());
	return meter.integrated();
}

// Skaliert ein Signal auf eine integrierte Lautheit (Stereo, beide Kanäle gleich)
inline void scaleTo(double rate, std::vector<float> &mono, float lufs)
{
	const float current = stereoLoudness(rate, mono);
	const float gain = static_cast<float>(std::pow(10.0, (lufs - current) / 20.0));
	for (float &sample : mono)
		sample *= gain;
}

inline void gainRange(std::vector<float> &signal, double rate, double from, double to, double db)
{
	const float gain = static_cast<float>(std::pow(10.0, db / 20.0));
	const size_t end = std::min(signal.size(), samples(rate, to));
	for (size_t i = samples(rate, from); i < end; i++)
		signal[i] *= gain;
}

} // namespace signals
