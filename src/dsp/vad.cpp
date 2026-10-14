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

#include "vad.hpp"

#include <algorithm>

namespace am::dsp {

namespace {

constexpr float kLevelMin = -100.0f;
constexpr float kFloorMin = -80.0f; // so leise rauscht kein echtes Mikrofon; digitale Stille zählt nicht
constexpr float kFloorMax = -15.0f;
constexpr float kSteadyDeviationDb = 3.0f; // schwankt der Pegel weniger, gilt ein Geräusch als gleichmäßig
constexpr float kDeviationSeconds = 0.3f;  // Zeitfenster für die Schwankung

} // namespace

void VoiceActivityDetector::configure(double framesPerSecond, const VadConfig &config)
{
	config_ = config;
	framesPerSecond_ = framesPerSecond > 0.0 ? framesPerSecond : 100.0;
	onsetFrames_ = std::max(1, static_cast<int>(std::lround(config_.onsetMs / 1000.0 * framesPerSecond_)));
	hangoverFrames_ = std::max(1, static_cast<int>(std::lround(config_.hangoverMs / 1000.0 * framesPerSecond_)));
	reset();
}

void VoiceActivityDetector::reset()
{
	floor_ = -60.0f;
	level_ = kSilence;
	mean_ = kLevelMin;
	meanSquare_ = kLevelMin * kLevelMin + 100.0f;
	onset_ = 0;
	hangover_ = 0;
	speaking_ = false;
}

void VoiceActivityDetector::processFrame(double power)
{
	level_ = std::max(powerToLufs(power), kLevelMin);
	const float frameSeconds = static_cast<float>(1.0 / framesPerSecond_);

	// Schwankung des Pegels über die letzte halbe Sekunde: Sprache schwankt stark (Silben und Lücken),
	// Lüfter und Rauschen kaum
	const float weight = std::min(1.0f, frameSeconds / kDeviationSeconds);
	mean_ += (level_ - mean_) * weight;
	meanSquare_ += (level_ * level_ - meanSquare_) * weight;
	const float deviation = std::sqrt(std::max(0.0f, meanSquare_ - mean_ * mean_));

	// Rauschboden: folgt leisen Stellen schnell, steigt sonst langsam (schneller bei gleichmäßigem Geräusch)
	if (level_ < floor_) {
		floor_ += (level_ - floor_) * std::min(1.0f, frameSeconds / 0.05f);
	} else {
		const float rise = deviation < kSteadyDeviationDb ? config_.steadyRiseDbPerS : config_.floorRiseDbPerS;
		floor_ = std::min(level_, floor_ + rise * frameSeconds);
	}
	floor_ = std::clamp(floor_, kFloorMin, kFloorMax);

	const bool loud = level_ > floor_ + config_.onDb && level_ > config_.minLufs;
	const bool stillLoud = level_ > floor_ + config_.offDb && level_ > config_.minLufs - 5.0f;
	if (!speaking_) {
		onset_ = loud ? onset_ + 1 : 0;
		if (onset_ >= onsetFrames_) {
			speaking_ = true;
			hangover_ = hangoverFrames_;
		}
	} else if (stillLoud) {
		hangover_ = hangoverFrames_;
	} else if (--hangover_ <= 0) {
		speaking_ = false;
		onset_ = 0;
	}
}

} // namespace am::dsp
