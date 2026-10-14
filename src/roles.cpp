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

#include "roles.hpp"

#include "text.hpp"

#include <obs-module.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace am {

namespace {

using Words = std::initializer_list<const char *>;

// Programme, deren Ton eine eindeutige Rolle hat
const Words kChatPrograms = {"discord",  "teamspeak", "ts3client", "mumble",  "zoom",    "skype",
			     "ms-teams", "teams",     "guilded",   "vesktop", "webcord", "element"};
const Words kMusicPrograms = {"spotify", "vlc",        "foobar2000",    "musicbee",  "aimp",
			      "itunes",  "applemusic", "tidal",         "deezer",    "amazon music",
			      "winamp",  "pretzel",    "youtube music", "soundcloud"};
const Words kBrowserPrograms = {"chrome", "firefox", "msedge", "opera", "brave", "vivaldi", "safari"};

// Schlüsselwörter im Namen der Quelle (klein geschrieben)
const Words kChatWords = {"discord", "teamspeak", "mumble", "chat", "call", "gäste", "gast ", "guest", "mitspieler"};
const Words kMusicWords = {"spotify", "musik", "music", "song", "playlist", "radio", "karaoke", "dj"};
const Words kAlertWords = {"alert",  "streamelements", "streamlabs", "sound", "sfx",    "tts",   "donation",
			   "spende", "follow",         "bits",       "hype",  "widget", "effekt"};
const Words kVoiceWords = {"mic", "mikro", "stimme", "voice", "headset", "rode", "shure", "yeti", "wave"};
const Words kGameWords = {"spiel", "game",     "konsole",   "console", "capture", "elgato", "hd60", "4k60",
			  "hdmi",  "cam link", "avermedia", "switch",  "ps5",     "ps4",    "xbox", "live gamer"};
const Words kWebcamWords = {"webcam", "facecam", "brio", "c920", "c922", "kiyo", "insta360", "obsbot"};
const Words kDesktopWords = {"stereo mix", "stereomix", "loopback", "was du hörst", "what u hear", "desktop"};

std::string lowercase(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

bool contains(const std::string &text, const Words &words)
{
	return std::any_of(words.begin(), words.end(),
			   [&](const char *word) { return text.find(word) != std::string::npos; });
}

bool typeIs(const std::string &type, Words types)
{
	return std::any_of(types.begin(), types.end(), [&](const char *t) { return type == t; });
}

std::string settingString(obs_source_t *source, const char *key)
{
	obs_data_t *settings = obs_source_get_settings(source);
	const std::string value = settings ? obs_data_get_string(settings, key) : "";
	obs_data_release(settings);
	return value;
}

// Gerätename aus den Eigenschaften (z. B. „Mikrofon (Shure MV7)“); die Geräte-ID allein sagt nichts
std::string deviceName(obs_source_t *source)
{
	const std::string id = settingString(source, "device_id");
	if (id.empty())
		return {};
	std::string name;
	obs_properties_t *properties = obs_source_properties(source);
	obs_property_t *list = properties ? obs_properties_get(properties, "device_id") : nullptr;
	for (size_t i = 0; list && i < obs_property_list_item_count(list); i++) {
		const char *value = obs_property_list_item_string(list, i);
		if (value && id == value) {
			name = obs_property_list_item_name(list, i);
			break;
		}
	}
	obs_properties_destroy(properties);
	return name;
}

// Rolle eines aufgenommenen Programms; Ignore, wenn nichts Eindeutiges
dsp::Role programRole(const std::string &program)
{
	if (contains(program, kChatPrograms))
		return dsp::Role::Chat;
	if (contains(program, kMusicPrograms))
		return dsp::Role::Music;
	if (contains(program, kBrowserPrograms))
		return dsp::Role::Desktop; // im Browser kann alles laufen
	return dsp::Role::Ignore;
}

} // namespace

std::string capturedExecutable(obs_source_t *source)
{
	// Windows: „Titel:Fensterklasse:programm.exe“, Doppelpunkte in den Teilen als „#3A“
	std::string window = settingString(source, "window");
	if (!window.empty()) {
		const size_t colon = window.rfind(':');
		std::string exe = colon == std::string::npos ? window : window.substr(colon + 1);
		for (size_t pos; (pos = exe.find("#3A")) != std::string::npos;)
			exe.replace(pos, 3, ":");
		return lowercase(exe);
	}
	// macOS ScreenCaptureKit: Bundle-ID der Anwendung
	const std::string bundle = settingString(source, "application");
	if (!bundle.empty())
		return lowercase(bundle);
	return {};
}

bool isDesktopCapture(obs_source_t *source)
{
	const std::string type = obs_source_get_unversioned_id(source);
	if (typeIs(type, {"wasapi_output_capture", "coreaudio_output_capture", "pulse_output_capture"}))
		return true;
	if (type == "sck_audio_capture") {
		obs_data_t *settings = obs_source_get_settings(source);
		const bool desktop = obs_data_get_int(settings, "type") == 0;
		obs_data_release(settings);
		return desktop;
	}
	return false;
}

dsp::Role suggestRole(obs_source_t *source)
{
	const std::string type = obs_source_get_unversioned_id(source);
	const std::string name = lowercase(obs_source_get_name(source));

	// Mikrofon- und Line-Eingänge: meist die Stimme, außer Capture-Cards und „Stereomix“
	if (typeIs(type,
		   {"wasapi_input_capture", "coreaudio_input_capture", "pulse_input_capture", "alsa_input_capture"})) {
		const std::string text = name + " " + lowercase(deviceName(source));
		if (contains(text, kChatWords))
			return dsp::Role::Chat;
		if (contains(text, kDesktopWords))
			return dsp::Role::Desktop;
		if (contains(text, kGameWords) && !contains(text, kVoiceWords))
			return dsp::Role::Game;
		return dsp::Role::Voice;
	}

	// Desktop-Ton: alles gemischt, außer der Name sagt etwas anderes
	if (isDesktopCapture(source)) {
		if (contains(name, kChatWords))
			return dsp::Role::Chat;
		if (contains(name, kMusicWords))
			return dsp::Role::Music;
		return dsp::Role::Desktop;
	}

	// Ton einer Anwendung oder eines Fensters: nach Programm, sonst ist es meist das Spiel
	if (typeIs(type, {"wasapi_process_output_capture", "sck_audio_capture", "window_capture", "game_capture",
			  "pipewire_audio_application_capture"})) {
		const dsp::Role role = programRole(capturedExecutable(source));
		if (role != dsp::Role::Ignore)
			return role;
		if (contains(name, kChatWords))
			return dsp::Role::Chat;
		if (contains(name, kMusicWords))
			return dsp::Role::Music;
		return dsp::Role::Game;
	}

	// Browserquellen: meist Alerts und Sounds
	if (type == "browser_source") {
		if (contains(name, kMusicWords))
			return dsp::Role::Music;
		if (contains(name, kChatWords))
			return dsp::Role::Chat;
		return dsp::Role::Alerts;
	}

	// Medienquellen: Musik oder Sounds
	if (typeIs(type, {"ffmpeg_source", "vlc_source"})) {
		if (contains(name, kMusicWords))
			return dsp::Role::Music;
		if (contains(name, kGameWords))
			return dsp::Role::Game;
		if (contains(name, kVoiceWords))
			return dsp::Role::Voice;
		return dsp::Role::Alerts;
	}

	// Videogeräte mit Ton: Capture-Card → Spiel, Webcam-Mikro lieber ignorieren (sonst doppelte Stimme)
	if (typeIs(type, {"dshow_input", "av_capture_input", "macos-avcapture", "v4l2_input"})) {
		const std::string text = name + " " + lowercase(settingString(source, "video_device_id")) + " " +
					 lowercase(settingString(source, "device_name"));
		if (contains(text, kWebcamWords))
			return dsp::Role::Ignore;
		return dsp::Role::Game;
	}

	// Unbekannter Typ: nur nach Name
	if (contains(name, kVoiceWords))
		return dsp::Role::Voice;
	if (contains(name, kChatWords))
		return dsp::Role::Chat;
	if (contains(name, kMusicWords))
		return dsp::Role::Music;
	if (contains(name, kAlertWords))
		return dsp::Role::Alerts;
	if (contains(name, kGameWords))
		return dsp::Role::Game;
	return dsp::Role::Ignore;
}

const char *roleName(dsp::Role role)
{
	switch (role) {
	case dsp::Role::Voice:
		return text("AutoMixer.Role.Voice");
	case dsp::Role::Game:
		return text("AutoMixer.Role.Game");
	case dsp::Role::Chat:
		return text("AutoMixer.Role.Chat");
	case dsp::Role::Music:
		return text("AutoMixer.Role.Music");
	case dsp::Role::Alerts:
		return text("AutoMixer.Role.Alerts");
	case dsp::Role::Desktop:
		return text("AutoMixer.Role.Desktop");
	default:
		return text("AutoMixer.Role.Ignore");
	}
}

} // namespace am
