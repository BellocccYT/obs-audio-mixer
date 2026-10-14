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

#include "types.hpp"

#include <cstring>

namespace am::dsp {

namespace {

const char *const kRoleIds[kRoleCount] = {"voice", "game", "chat", "music", "alerts", "desktop", "ignore"};

} // namespace

const char *roleId(Role role)
{
	const int index = static_cast<int>(role);
	return index >= 0 && index < kRoleCount ? kRoleIds[index] : "ignore";
}

bool roleFromId(const char *id, Role &role)
{
	if (!id)
		return false;
	for (int i = 0; i < kRoleCount; i++) {
		if (std::strcmp(id, kRoleIds[i]) == 0) {
			role = static_cast<Role>(i);
			return true;
		}
	}
	return false;
}

} // namespace am::dsp
