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

#include "dsp/types.hpp"

#include <obs.h>

#include <string>

namespace am {

// Schlägt eine Rolle vor: nach Quellentyp, aufgenommenem Programm und Name. Der Nutzer kann alles ändern.
// Beispiele: Mikrofon → Stimme, Anwendungsaudio von discord.exe → Chat, Spotify → Musik,
// Spielaufnahme → Spiel, Browserquelle mit „alert“ im Namen → Alerts.
dsp::Role suggestRole(obs_source_t *source);

// Fängt die Quelle den gesamten Desktop-Ton ein (Spiel, Musik und Discord gemischt)?
bool isDesktopCapture(obs_source_t *source);

// Programm, dessen Ton oder Fenster die Quelle aufnimmt (z. B. „discord.exe“), sonst leer
std::string capturedExecutable(obs_source_t *source);

// Übersetzter Name einer Rolle
const char *roleName(dsp::Role role);

} // namespace am
