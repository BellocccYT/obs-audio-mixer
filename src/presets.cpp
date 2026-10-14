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

#include "presets.hpp"

#include "settings.hpp"
#include "text.hpp"

// obs-module.h muss vor plugin-support.h stehen, sonst kollidiert die blogva-Deklaration
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>

namespace am {

namespace {

constexpr dsp::Role kTargetRoles[] = {dsp::Role::Voice, dsp::Role::Game,   dsp::Role::Chat,
				      dsp::Role::Music, dsp::Role::Alerts, dsp::Role::Desktop};

// Rollenliste wie „game, music, desktop“ (obs_data kennt keine Arrays aus einfachen Strings)
std::array<bool, dsp::kRoleCount> parseRoles(const char *text)
{
	std::array<bool, dsp::kRoleCount> roles{};
	std::string list = text ? text : "";
	size_t start = 0;
	while (start <= list.size()) {
		size_t end = list.find(',', start);
		if (end == std::string::npos)
			end = list.size();
		std::string id = list.substr(start, end - start);
		id.erase(0, id.find_first_not_of(" \t"));
		id.erase(id.find_last_not_of(" \t") + 1);
		dsp::Role role;
		if (!id.empty() && dsp::roleFromId(id.c_str(), role))
			roles[static_cast<size_t>(role)] = true;
		start = end + 1;
	}
	return roles;
}

std::string joinRoles(const std::array<bool, dsp::kRoleCount> &roles)
{
	std::string list;
	for (int i = 0; i < dsp::kRoleCount; i++) {
		if (!roles[static_cast<size_t>(i)])
			continue;
		if (!list.empty())
			list += ", ";
		list += dsp::roleId(static_cast<dsp::Role>(i));
	}
	return list;
}

dsp::Preset presetFromData(obs_data_t *data, bool builtin)
{
	dsp::Preset preset = fallbackPreset();
	preset.id = obs_data_get_string(data, "id");
	preset.name = obs_data_get_string(data, "name");
	preset.description = obs_data_get_string(data, "description");
	preset.builtin = builtin;
	dsp::roleFromId(obs_data_get_string(data, "anchor"), preset.anchor);
	if (obs_data_has_user_value(data, "target"))
		preset.targetLufs = std::clamp(static_cast<float>(obs_data_get_double(data, "target")), -40.0f, -6.0f);

	obs_data_t *offsets = obs_data_get_obj(data, "offsets");
	for (const dsp::Role role : kTargetRoles) {
		const char *key = dsp::roleId(role);
		if (offsets && obs_data_has_user_value(offsets, key))
			preset.offsets[static_cast<size_t>(role)] =
				std::clamp(static_cast<float>(obs_data_get_double(offsets, key)), -40.0f, 20.0f);
	}
	obs_data_release(offsets);
	preset.offsets[static_cast<size_t>(preset.anchor)] = 0.0f;

	obs_data_t *ducking = obs_data_get_obj(data, "ducking");
	if (ducking) {
		preset.duckDb = std::clamp(static_cast<float>(obs_data_get_double(ducking, "amount")), 0.0f, 30.0f);
		if (obs_data_has_user_value(ducking, "roles"))
			preset.ducked = parseRoles(obs_data_get_string(ducking, "roles"));
		if (obs_data_has_user_value(ducking, "triggers"))
			preset.duckTriggers = parseRoles(obs_data_get_string(ducking, "triggers"));
		if (obs_data_has_user_value(ducking, "hold_ms"))
			preset.duckHoldMs =
				std::clamp(static_cast<float>(obs_data_get_double(ducking, "hold_ms")), 0.0f, 5000.0f);
		obs_data_release(ducking);
	}
	dsp::speedFromId(obs_data_get_string(data, "speed"), preset.speed);
	if (obs_data_has_user_value(data, "scream_guard"))
		preset.screamGuard = obs_data_get_bool(data, "scream_guard");

	// Mitgelieferte Presets: Name und Beschreibung aus der Übersetzung, falls vorhanden
	if (builtin) {
		const std::string nameKey = "AutoMixer.Preset." + preset.id;
		const std::string descriptionKey = nameKey + ".Description";
		const char *name = nullptr;
		if (obs_module_get_string(nameKey.c_str(), &name) && name)
			preset.name = name;
		const char *description = nullptr;
		if (obs_module_get_string(descriptionKey.c_str(), &description) && description)
			preset.description = description;
	}
	if (preset.name.empty())
		preset.name = preset.id;
	return preset;
}

obs_data_t *presetToData(const dsp::Preset &preset)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", preset.id.c_str());
	obs_data_set_string(data, "name", preset.name.c_str());
	obs_data_set_string(data, "description", preset.description.c_str());
	obs_data_set_string(data, "anchor", dsp::roleId(preset.anchor));
	obs_data_set_double(data, "target", preset.targetLufs);
	obs_data_t *offsets = obs_data_create();
	for (const dsp::Role role : kTargetRoles)
		obs_data_set_double(offsets, dsp::roleId(role), preset.offset(role));
	obs_data_set_obj(data, "offsets", offsets);
	obs_data_release(offsets);
	obs_data_t *ducking = obs_data_create();
	obs_data_set_double(ducking, "amount", preset.duckDb);
	obs_data_set_string(ducking, "roles", joinRoles(preset.ducked).c_str());
	obs_data_set_string(ducking, "triggers", joinRoles(preset.duckTriggers).c_str());
	obs_data_set_double(ducking, "hold_ms", preset.duckHoldMs);
	obs_data_set_obj(data, "ducking", ducking);
	obs_data_release(ducking);
	obs_data_set_string(data, "speed", dsp::speedId(preset.speed));
	obs_data_set_bool(data, "scream_guard", preset.screamGuard);
	return data;
}

std::vector<dsp::Preset> presetsFromFile(const char *path, bool builtin)
{
	std::vector<dsp::Preset> presets;
	obs_data_t *data = obs_data_create_from_json_file(path);
	if (!data)
		return presets;
	obs_data_array_t *array = obs_data_get_array(data, "presets");
	for (size_t i = 0; array && i < obs_data_array_count(array); i++) {
		obs_data_t *item = obs_data_array_item(array, i);
		dsp::Preset preset = presetFromData(item, builtin);
		if (!preset.id.empty() && preset.id != "auto")
			presets.push_back(preset);
		obs_data_release(item);
	}
	obs_data_array_release(array);
	obs_data_release(data);
	return presets;
}

} // namespace

dsp::Preset fallbackPreset()
{
	dsp::Preset preset;
	preset.id = "voice-first";
	preset.name = text("AutoMixer.Preset.voice-first");
	preset.offsets = {0.0f, -10.0f, -1.0f, -18.0f, -4.0f, -10.0f, 0.0f};
	preset.duckDb = 4.0f;
	preset.ducked[static_cast<size_t>(dsp::Role::Game)] = true;
	preset.ducked[static_cast<size_t>(dsp::Role::Music)] = true;
	preset.ducked[static_cast<size_t>(dsp::Role::Desktop)] = true;
	preset.duckTriggers[static_cast<size_t>(dsp::Role::Voice)] = true;
	preset.builtin = true;
	return preset;
}

std::vector<dsp::Preset> loadBuiltinPresets()
{
	char *path = obs_module_file("presets.json");
	std::vector<dsp::Preset> presets = path ? presetsFromFile(path, true) : std::vector<dsp::Preset>();
	bfree(path);
	if (presets.empty()) {
		obs_log(LOG_WARNING, "presets.json nicht lesbar, nutze Standardwerte");
		presets.push_back(fallbackPreset());
	}
	return presets;
}

std::vector<dsp::Preset> loadUserPresets()
{
	const std::string path = configPath("presets.json");
	std::vector<dsp::Preset> presets;
	if (os_file_exists(path.c_str()))
		presets = presetsFromFile(path.c_str(), false);
	return presets;
}

void saveUserPresets(const std::vector<dsp::Preset> &presets)
{
	os_mkdirs(configPath("").c_str());
	exportPresets(configPath("presets.json"), presets);
}

bool importPresets(const std::string &path, std::vector<dsp::Preset> &presets, std::string &error)
{
	presets = presetsFromFile(path.c_str(), false);
	if (presets.empty()) {
		// Auch ein einzelnes Preset ohne Hülle annehmen
		obs_data_t *data = obs_data_create_from_json_file(path.c_str());
		if (data && obs_data_has_user_value(data, "offsets"))
			presets.push_back(presetFromData(data, false));
		obs_data_release(data);
	}
	if (presets.empty()) {
		error = text("AutoMixer.Presets.ImportFailed");
		return false;
	}
	return true;
}

bool exportPresets(const std::string &path, const std::vector<dsp::Preset> &presets)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "version", 1);
	obs_data_array_t *array = obs_data_array_create();
	for (const dsp::Preset &preset : presets) {
		obs_data_t *item = presetToData(preset);
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "presets", array);
	obs_data_array_release(array);
	const bool ok = obs_data_save_json_pretty_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
	return ok;
}

std::string newUserPresetId(const std::vector<dsp::Preset> &existing)
{
	for (int i = 1;; i++) {
		const std::string id = "user-" + std::to_string(i);
		if (std::none_of(existing.begin(), existing.end(), [&](const dsp::Preset &p) { return p.id == id; }))
			return id;
	}
}

} // namespace am
