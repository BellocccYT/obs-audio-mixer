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

#include "calibration.hpp"

#include <algorithm>

namespace am::dsp {

namespace {

constexpr float kMinActiveSeconds = 3.0f;
constexpr float kNoisyFloorLufs = -50.0f;

} // namespace

void CalibrationCollector::reset()
{
	sum_ = 0.0;
	active_ = 0.0f;
	maxTruePeak_ = kSilence;
	floorSum_ = 0.0;
	floorTime_ = 0.0f;
}

void CalibrationCollector::add(float dt, const ChannelInput &in, float activityGateLufs)
{
	if (!in.present)
		return;
	maxTruePeak_ = std::max(maxTruePeak_, in.truePeak);
	if (isSpeechRole(in.role) && !isSilent(in.noiseFloor)) {
		floorSum_ += static_cast<double>(in.noiseFloor) * dt;
		floorTime_ += dt;
	}
	float level = kSilence;
	if (isSpeechRole(in.role)) {
		if (in.speaking)
			level = isSilent(in.speech) ? in.momentary : in.speech;
	} else if (in.momentary > activityGateLufs) {
		level = in.shortTerm;
	}
	if (!isSilent(level)) {
		sum_ += static_cast<double>(level) * dt;
		active_ += dt;
	}
}

CalibrationResult CalibrationCollector::result(float targetLufs, const ChannelInput &in) const
{
	CalibrationResult result;
	result.activeSeconds = active_;
	result.maxTruePeak = maxTruePeak_;
	result.noiseFloor = floorTime_ > 0.0f ? static_cast<float>(floorSum_ / floorTime_) : kSilence;
	if (!isSilent(result.noiseFloor) && result.noiseFloor > kNoisyFloorLufs)
		result.hints |= kHintNoisy;
	result.enoughData = active_ >= kMinActiveSeconds;
	if (!result.enoughData) {
		result.hints |= kHintNoSignal;
		return result;
	}
	result.measuredLufs = static_cast<float>(sum_ / active_);
	const float needed = targetLufs - result.measuredLufs - in.faderDb;
	result.suggestedGainDb = std::clamp(needed, in.minGainDb, in.maxGainDb);
	if (needed > in.maxGainDb)
		result.hints |= kHintTooQuiet;
	if (needed < in.minGainDb)
		result.hints |= kHintTooLoud;
	if (!isSilent(maxTruePeak_) && maxTruePeak_ > -1.0f)
		result.hints |= kHintClipping;
	return result;
}

void CalibrationCollector::apply(const CalibrationResult &result, ChannelState &state)
{
	if (!result.enoughData)
		return;
	state.estimate = result.measuredLufs;
	state.activeSeconds = std::max(state.activeSeconds, static_cast<double>(result.activeSeconds));
	state.levelGain = result.suggestedGainDb;
	state.reportedLevel = result.suggestedGainDb;
	state.moving = false;
}

} // namespace am::dsp
