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

#include <string>

namespace am {

// Übersetzter Text aus data/locale/*.ini
const char *tr(const char *key);

// Übersetzter Text mit printf-Platzhaltern, z. B. trf("AutoMixer.History.Ducking", "Spiel", 4.0)
std::string trf(const char *key, ...);

// „+3,5 dB“ / „−4 dB“ mit echtem Minuszeichen; Dezimaltrennzeichen je nach Sprache
std::string formatDb(float db, bool plus = true);

// „−15,2 LUFS“, „–“ bei Stille
std::string formatLufs(float lufs);

} // namespace am
