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

#include "settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>

namespace am {

namespace {

// Kleine Helfer: Werte nur übernehmen, wenn sie in der Datei stehen, und auf sinnvolle Bereiche begrenzen
struct Reader {
	obs_data_t *data;

	void get(const char *key, bool &out) const
	{
		if (obs_data_has_user_value(data, key))
			out = obs_data_get_bool(data, key);
	}
	void get(const char *key, float &out, float min, float max) const
	{
		if (obs_data_has_user_value(data, key))
			out = std::clamp(static_cast<float>(obs_data_get_double(data, key)), min, max);
	}
	void get(const char *key, int &out, int min, int max) const
	{
		if (obs_data_has_user_value(data, key))
			out = std::clamp(static_cast<int>(obs_data_get_int(data, key)), min, max);
	}
	void get(const char *key, std::string &out) const
	{
		if (obs_data_has_user_value(data, key))
			out = obs_data_get_string(data, key);
	}
};

} // namespace

std::string configPath(const char *file)
{
	char *path = obs_module_config_path(file);
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

SourceSettings *Settings::findSource(const std::string &uuid, const std::string &name)
{
	for (SourceSettings &source : sources) {
		if (!uuid.empty() && source.uuid == uuid)
			return &source;
	}
	// Rückfall über den Namen, z. B. wenn die Szenensammlung neu angelegt wurde
	for (SourceSettings &source : sources) {
		if (!name.empty() && source.name == name)
			return &source;
	}
	return nullptr;
}

const ScenePreset *Settings::findScene(const std::string &scene) const
{
	for (const ScenePreset &entry : scenePresets) {
		if (entry.scene == scene)
			return &entry;
	}
	return nullptr;
}

Settings loadSettings()
{
	Settings s;
	obs_data_t *data = obs_data_create_from_json_file_safe(configPath("settings.json").c_str(), "bak");
	if (!data)
		return s;
	const Reader r{data};

	r.get("enabled", s.enabled);
	std::string mode;
	r.get("mode", mode);
	s.mode = mode == "regulate" ? Mode::Regulate : Mode::Observe;
	r.get("preset", s.preset);
	r.get("filter_consent", s.filterConsent);
	r.get("voice_target", s.voiceTargetLufs, -30.0f, -6.0f);
	r.get("mix_target", s.mixTargetLufs, -30.0f, -6.0f);
	r.get("mix_max", s.mixMaxLufs, -24.0f, 0.0f);
	r.get("ceiling", s.ceilingDbtp, -6.0f, 0.0f);
	r.get("stream_track", s.streamTrack, 0, 6);
	r.get("lock_on_fader", s.lockOnFader);
	r.get("min_gain", s.defaultMinGainDb, -60.0f, 0.0f);
	r.get("max_gain", s.defaultMaxGainDb, 0.0f, 24.0f);
	r.get("warn_loud_lu", s.warnings.loudLu, 1.0f, 20.0f);
	r.get("warn_quiet_lu", s.warnings.quietLu, 1.0f, 30.0f);
	r.get("warn_loud_seconds", s.warnings.loudSeconds, 0.5f, 60.0f);
	r.get("warn_quiet_seconds", s.warnings.quietSeconds, 0.5f, 120.0f);
	r.get("warn_clip_dbtp", s.warnings.clipDbtp, -6.0f, 0.0f);
	r.get("mute_warning", s.muteWarning);
	r.get("auto_by_activity", s.autoByActivity);
	r.get("dock_introduced", s.dockIntroduced);

	obs_data_array_t *scenes = obs_data_get_array(data, "scene_presets");
	for (size_t i = 0; scenes && i < obs_data_array_count(scenes); i++) {
		obs_data_t *item = obs_data_array_item(scenes, i);
		ScenePreset entry{obs_data_get_string(item, "scene"), obs_data_get_string(item, "preset")};
		if (!entry.scene.empty() && !entry.preset.empty())
			s.scenePresets.push_back(entry);
		obs_data_release(item);
	}
	obs_data_array_release(scenes);

	obs_data_array_t *sources = obs_data_get_array(data, "sources");
	for (size_t i = 0; sources && i < obs_data_array_count(sources); i++) {
		obs_data_t *item = obs_data_array_item(sources, i);
		const Reader ri{item};
		SourceSettings entry;
		entry.minGainDb = s.defaultMinGainDb;
		entry.maxGainDb = s.defaultMaxGainDb;
		ri.get("uuid", entry.uuid);
		ri.get("name", entry.name);
		dsp::roleFromId(obs_data_get_string(item, "role"), entry.role);
		ri.get("role_chosen", entry.roleChosen);
		ri.get("locked", entry.locked);
		ri.get("min_gain", entry.minGainDb, -60.0f, 0.0f);
		ri.get("max_gain", entry.maxGainDb, 0.0f, 24.0f);
		if (!entry.uuid.empty() || !entry.name.empty())
			s.sources.push_back(entry);
		obs_data_release(item);
	}
	obs_data_array_release(sources);

	obs_data_release(data);
	return s;
}

void saveSettings(const Settings &s)
{
	const std::string dir = configPath("");
	os_mkdirs(dir.c_str());

	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", s.enabled);
	obs_data_set_string(data, "mode", s.mode == Mode::Regulate ? "regulate" : "observe");
	obs_data_set_string(data, "preset", s.preset.c_str());
	obs_data_set_bool(data, "filter_consent", s.filterConsent);
	obs_data_set_double(data, "voice_target", s.voiceTargetLufs);
	obs_data_set_double(data, "mix_target", s.mixTargetLufs);
	obs_data_set_double(data, "mix_max", s.mixMaxLufs);
	obs_data_set_double(data, "ceiling", s.ceilingDbtp);
	obs_data_set_int(data, "stream_track", s.streamTrack);
	obs_data_set_bool(data, "lock_on_fader", s.lockOnFader);
	obs_data_set_double(data, "min_gain", s.defaultMinGainDb);
	obs_data_set_double(data, "max_gain", s.defaultMaxGainDb);
	obs_data_set_double(data, "warn_loud_lu", s.warnings.loudLu);
	obs_data_set_double(data, "warn_quiet_lu", s.warnings.quietLu);
	obs_data_set_double(data, "warn_loud_seconds", s.warnings.loudSeconds);
	obs_data_set_double(data, "warn_quiet_seconds", s.warnings.quietSeconds);
	obs_data_set_double(data, "warn_clip_dbtp", s.warnings.clipDbtp);
	obs_data_set_bool(data, "mute_warning", s.muteWarning);
	obs_data_set_bool(data, "auto_by_activity", s.autoByActivity);
	obs_data_set_bool(data, "dock_introduced", s.dockIntroduced);

	obs_data_array_t *scenes = obs_data_array_create();
	for (const ScenePreset &entry : s.scenePresets) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "scene", entry.scene.c_str());
		obs_data_set_string(item, "preset", entry.preset.c_str());
		obs_data_array_push_back(scenes, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "scene_presets", scenes);
	obs_data_array_release(scenes);

	obs_data_array_t *sources = obs_data_array_create();
	for (const SourceSettings &entry : s.sources) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "uuid", entry.uuid.c_str());
		obs_data_set_string(item, "name", entry.name.c_str());
		obs_data_set_string(item, "role", dsp::roleId(entry.role));
		obs_data_set_bool(item, "role_chosen", entry.roleChosen);
		obs_data_set_bool(item, "locked", entry.locked);
		obs_data_set_double(item, "min_gain", entry.minGainDb);
		obs_data_set_double(item, "max_gain", entry.maxGainDb);
		obs_data_array_push_back(sources, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "sources", sources);
	obs_data_array_release(sources);

	obs_data_save_json_safe(data, configPath("settings.json").c_str(), "tmp", "bak");
	obs_data_release(data);
}

} // namespace am
