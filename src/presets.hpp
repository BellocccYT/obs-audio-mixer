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

#include "dsp/preset.hpp"

#include <string>
#include <vector>

namespace am {

// Mitgelieferte Presets (data/presets.json) und eigene Presets des Nutzers (presets.json im
// Konfigurationsordner). Das Preset „Automatisch“ ist kein Datensatz, es wählt selbst unter den anderen.
std::vector<dsp::Preset> loadBuiltinPresets();
std::vector<dsp::Preset> loadUserPresets();
void saveUserPresets(const std::vector<dsp::Preset> &presets);

// Import und Export als JSON-Datei (ein oder mehrere Presets)
bool importPresets(const std::string &path, std::vector<dsp::Preset> &presets, std::string &error);
bool exportPresets(const std::string &path, const std::vector<dsp::Preset> &presets);

// Neutraler Rückfall, falls data/presets.json fehlt oder kaputt ist
dsp::Preset fallbackPreset();

// Freie ID für ein eigenes Preset
std::string newUserPresetId(const std::vector<dsp::Preset> &existing);

} // namespace am
