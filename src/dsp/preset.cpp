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

#include "preset.hpp"

#include <cstring>

namespace am::dsp {

namespace {

const char *const kSpeedIds[kSpeedCount] = {"slow", "normal", "fast"};

const SpeedProfile kSpeedProfiles[kSpeedCount] = {
	{3.0f, 0.75f, 240.0f, 3.0f, 2.0f}, // langsam
	{6.0f, 1.5f, 120.0f, 5.0f, 3.0f},  // normal
	{10.0f, 3.0f, 45.0f, 8.0f, 5.0f},  // schnell
};

} // namespace

const char *speedId(Speed speed)
{
	const int index = static_cast<int>(speed);
	return index >= 0 && index < kSpeedCount ? kSpeedIds[index] : "normal";
}

bool speedFromId(const char *id, Speed &speed)
{
	if (!id)
		return false;
	for (int i = 0; i < kSpeedCount; i++) {
		if (std::strcmp(id, kSpeedIds[i]) == 0) {
			speed = static_cast<Speed>(i);
			return true;
		}
	}
	return false;
}

const SpeedProfile &speedProfile(Speed speed)
{
	const int index = static_cast<int>(speed);
	return kSpeedProfiles[index >= 0 && index < kSpeedCount ? index : 1];
}

} // namespace am::dsp
