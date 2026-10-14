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

#include "hotkeys.hpp"

#include "engine.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <array>
#include <string>

namespace am {

namespace {

constexpr int kPresetHotkeys = 9;

enum class Action { Preset, Auto, Toggle, Relearn };

struct Hotkey {
	std::string name;
	Action action = Action::Preset;
	int index = 0; // bei Preset: 0–8
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

Engine *g_engine = nullptr;
std::array<Hotkey, kPresetHotkeys + 3> g_hotkeys;
bool g_saving = false;

std::string presetDescription(int index)
{
	std::string description = textf("AutoMixer.Hotkey.Preset", index + 1);
	if (g_engine) {
		const std::vector<dsp::Preset> presets = g_engine->presets();
		if (index < static_cast<int>(presets.size()))
			description += " (" + presets[static_cast<size_t>(index)].name + ")";
	}
	return description;
}

// Läuft im Hotkey-Thread von OBS; die Engine ist threadsicher
void onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_engine)
		return;
	const Hotkey &hotkey = *static_cast<Hotkey *>(data);
	switch (hotkey.action) {
	case Action::Preset: {
		const std::vector<dsp::Preset> presets = g_engine->presets();
		if (hotkey.index < static_cast<int>(presets.size()))
			g_engine->selectPreset(presets[static_cast<size_t>(hotkey.index)].id);
		break;
	}
	case Action::Auto:
		g_engine->selectPreset(dsp::preset_ids::kAuto);
		break;
	case Action::Toggle:
		g_engine->setEnabled(!g_engine->settings().enabled);
		break;
	case Action::Relearn:
		g_engine->relearn();
		break;
	}
}

void onSave(obs_data_t *data, bool saving, void *)
{
	for (Hotkey &hotkey : g_hotkeys) {
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			continue;
		if (saving) {
			obs_data_array_t *bindings = obs_hotkey_save(hotkey.id);
			obs_data_set_array(data, hotkey.name.c_str(), bindings);
			obs_data_array_release(bindings);
		} else {
			obs_data_array_t *bindings = obs_data_get_array(data, hotkey.name.c_str());
			obs_hotkey_load(hotkey.id, bindings);
			obs_data_array_release(bindings);
		}
	}
}

} // namespace

void registerHotkeys(Engine *engine)
{
	g_engine = engine;
	for (int i = 0; i < kPresetHotkeys; i++) {
		Hotkey &hotkey = g_hotkeys[static_cast<size_t>(i)];
		hotkey.name = "auto_mixer_preset_" + std::to_string(i + 1);
		hotkey.action = Action::Preset;
		hotkey.index = i;
	}
	g_hotkeys[kPresetHotkeys] = {"auto_mixer_preset_auto", Action::Auto, 0, OBS_INVALID_HOTKEY_ID};
	g_hotkeys[kPresetHotkeys + 1] = {"auto_mixer_toggle", Action::Toggle, 0, OBS_INVALID_HOTKEY_ID};
	g_hotkeys[kPresetHotkeys + 2] = {"auto_mixer_relearn", Action::Relearn, 0, OBS_INVALID_HOTKEY_ID};

	for (Hotkey &hotkey : g_hotkeys) {
		std::string description;
		switch (hotkey.action) {
		case Action::Preset:
			description = presetDescription(hotkey.index);
			break;
		case Action::Auto:
			description = text("AutoMixer.Hotkey.Auto");
			break;
		case Action::Toggle:
			description = text("AutoMixer.Hotkey.Toggle");
			break;
		case Action::Relearn:
			description = text("AutoMixer.Hotkey.Relearn");
			break;
		}
		hotkey.id = obs_hotkey_register_frontend(hotkey.name.c_str(), description.c_str(), onHotkey, &hotkey);
	}
	obs_frontend_add_save_callback(onSave, nullptr);
	g_saving = true;
}

void stopHotkeySaving()
{
	if (g_saving)
		obs_frontend_remove_save_callback(onSave, nullptr);
	g_saving = false;
}

void unregisterHotkeys()
{
	stopHotkeySaving();
	for (Hotkey &hotkey : g_hotkeys) {
		if (hotkey.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(hotkey.id);
		hotkey.id = OBS_INVALID_HOTKEY_ID;
	}
	g_engine = nullptr;
}

void updateHotkeyDescriptions()
{
	for (const Hotkey &hotkey : g_hotkeys) {
		if (hotkey.action == Action::Preset && hotkey.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_set_description(hotkey.id, presetDescription(hotkey.index).c_str());
	}
}

} // namespace am
