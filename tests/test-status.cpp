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

// Warnungen erst nach einer Mindestdauer, Übersteuerung sofort

#include "test-framework.hpp"

#include <dsp/status.hpp>

using namespace am::dsp;

namespace {

constexpr float kDt = 0.05f;

Status runFor(StatusTracker &tracker, float seconds, bool active, float level, float target, float peak = -20.0f)
{
	Status status = tracker.status();
	for (float t = 0.0f; t < seconds - 1e-4f; t += kDt)
		status = tracker.update(kDt, active, level, target, peak, false);
	return status;
}

} // namespace

TEST_CASE("status", too_loud_only_after_minimum_duration)
{
	StatusTracker tracker;
	CHECK(runFor(tracker, 1.0f, true, -16.0f, -16.0f) == Status::Ok);
	// 6 LU zu laut: nach 2 s noch keine Warnung, nach 3 s schon
	CHECK(runFor(tracker, 2.0f, true, -10.0f, -16.0f) == Status::Ok);
	CHECK(runFor(tracker, 1.1f, true, -10.0f, -16.0f) == Status::TooLoud);
	// Kurz wieder ok: Warnung bleibt noch, nach 1 s verschwindet sie
	CHECK(runFor(tracker, 0.5f, true, -16.0f, -16.0f) == Status::TooLoud);
	CHECK(runFor(tracker, 0.6f, true, -16.0f, -16.0f) == Status::Ok);
}

TEST_CASE("status", single_peak_is_no_warning)
{
	StatusTracker tracker;
	runFor(tracker, 1.0f, true, -16.0f, -16.0f);
	// 1 s Ausreißer reicht nicht
	runFor(tracker, 1.0f, true, -8.0f, -16.0f);
	CHECK(runFor(tracker, 3.0f, true, -16.0f, -16.0f) == Status::Ok);
}

TEST_CASE("status", too_quiet)
{
	StatusTracker tracker;
	CHECK(runFor(tracker, 5.0f, true, -30.0f, -16.0f) == Status::Ok);
	CHECK(runFor(tracker, 1.1f, true, -30.0f, -16.0f) == Status::TooQuiet);
}

TEST_CASE("status", clipping_is_immediate)
{
	StatusTracker tracker;
	runFor(tracker, 1.0f, true, -16.0f, -16.0f);
	CHECK(tracker.update(kDt, true, -16.0f, -16.0f, 0.2f, false) == Status::Clipping);
	// bleibt eine Weile stehen
	CHECK(runFor(tracker, 2.0f, true, -16.0f, -16.0f) == Status::Clipping);
	CHECK(runFor(tracker, 1.5f, true, -16.0f, -16.0f) == Status::Ok);
	CHECK(tracker.update(kDt, true, -16.0f, -16.0f, -20.0f, true) == Status::Clipping);
}

TEST_CASE("status", inactive_when_silent)
{
	StatusTracker tracker;
	runFor(tracker, 1.0f, true, -16.0f, -16.0f);
	CHECK(runFor(tracker, 1.0f, false, kSilence, -16.0f) == Status::Ok);
	CHECK(runFor(tracker, 1.5f, false, kSilence, -16.0f) == Status::Inactive);
}
