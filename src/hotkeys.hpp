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

namespace am {

class Engine;

// Tastenkürzel unter OBS-Einstellungen → Tastenkürzel: Preset 1–9, Automatisch, Pausieren/Fortsetzen und
// Neu einmessen. Die Belegung speichert OBS mit der Szenensammlung.
void registerHotkeys(Engine *engine);
void stopHotkeySaving(); // beim Beenden, bevor die Frontend-API verschwindet
void unregisterHotkeys();

// Namen der Presets in die Beschreibungen übernehmen (nach dem Laden und nach Änderungen an den Presets)
void updateHotkeyDescriptions();

} // namespace am
