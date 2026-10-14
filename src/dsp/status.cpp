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

#include "status.hpp"

#include <algorithm>

namespace am::dsp {

void StatusTracker::reset()
{
	status_ = Status::Inactive;
	loudFor_ = quietFor_ = okFor_ = inactiveFor_ = clipHold_ = 0.0f;
}

Status StatusTracker::update(float dt, bool active, float effective, float target, float truePeak, bool clipped)
{
	// Übersteuerung sofort melden und eine Weile stehen lassen
	if (clipped || (!isSilent(truePeak) && truePeak >= config_.clipDbtp))
		clipHold_ = config_.clipHoldSeconds;
	else
		clipHold_ = std::max(0.0f, clipHold_ - dt);

	const bool known = active && !isSilent(effective) && !isSilent(target);
	const bool loud = known && effective > target + config_.loudLu;
	const bool quiet = known && effective < target - config_.quietLu;
	loudFor_ = loud ? loudFor_ + dt : 0.0f;
	quietFor_ = quiet ? quietFor_ + dt : 0.0f;
	okFor_ = known && !loud && !quiet ? okFor_ + dt : 0.0f;
	inactiveFor_ = active ? 0.0f : inactiveFor_ + dt;

	if (clipHold_ > 0.0f) {
		status_ = Status::Clipping;
	} else if (loudFor_ >= config_.loudSeconds) {
		status_ = Status::TooLoud;
	} else if (quietFor_ >= config_.quietSeconds) {
		status_ = Status::TooQuiet;
	} else if (status_ == Status::TooLoud || status_ == Status::TooQuiet || status_ == Status::Clipping) {
		// Warnung erst zurücknehmen, wenn es eine Weile wieder passt
		if (okFor_ >= config_.clearSeconds || inactiveFor_ >= config_.clearSeconds)
			status_ = known ? Status::Ok : Status::Inactive;
		if (status_ == Status::Clipping)
			status_ = known ? Status::Ok : Status::Inactive;
	} else if (known) {
		status_ = Status::Ok;
	} else if (inactiveFor_ >= 2.0f) {
		status_ = Status::Inactive;
	}
	return status_;
}

} // namespace am::dsp
