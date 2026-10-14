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

#include "settings.hpp"

#include "dsp/preset.hpp"
#include "dsp/regulator.hpp"
#include "dsp/status.hpp"
#include "dsp/types.hpp"

#include <obs.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace am {

constexpr int kTrackCount = 6;

struct HistoryEntry {
	int64_t time = 0; // Sekunden seit 1970
	std::string text;
};

enum class WarningKind { MixTooLoud, MixClipping, MicMuted, DesktopOnly };

struct Warning {
	WarningKind kind;
	std::string text;
	bool severe = true; // false: nur ein Hinweis
};

// Was das Dock über eine Quelle anzeigt
struct SourceView {
	std::string uuid;
	std::string name;
	std::string type; // Quellentyp (unversionierte ID)
	dsp::Role role = dsp::Role::Ignore;
	bool locked = false;
	bool hasFilter = false; // Auto-Mixer-Filter vorhanden und eingeschaltet
	bool inProgram = false; // im Programm aktiv
	bool muted = false;
	bool speaking = false;
	bool present = false;            // liefert gerade Audio
	float momentary = dsp::kSilence; // im Mix: nach Anpassung und Fader (LUFS)
	float shortTerm = dsp::kSilence;
	float truePeak = dsp::kSilence; // dBTP im Mix
	float targetLufs = dsp::kSilence;
	float gainDb = 0.0f;          // aktuelle Anpassung durch den Filter
	float suggestedGainDb = 0.0f; // was die Regelung tun will (im Modus „Nur beobachten“ nur Vorschlag)
	float faderDb = 0.0f;
	dsp::Status status = dsp::Status::Inactive;
};

// Alle Audioquellen für den Einstellungsdialog, auch stille
struct KnownSource {
	std::string uuid;
	std::string name;
	std::string type;
	dsp::Role role = dsp::Role::Ignore;
	dsp::Role suggested = dsp::Role::Ignore;
	bool locked = false;
	bool hasFilter = false;
	float minGainDb = -30.0f;
	float maxGainDb = 12.0f;
};

struct TrackView {
	bool present = false; // auf der Spur kommt Audio an
	float momentary = dsp::kSilence;
	float shortTerm = dsp::kSilence;
	float truePeak = dsp::kSilence;
	dsp::Status status = dsp::Status::Inactive;
};

struct EngineView {
	bool running = false;
	bool enabled = true;
	Mode mode = Mode::Observe;
	std::string presetId;       // gewählt (auch „auto“)
	std::string activePresetId; // gerade wirksam
	int streamTrack = 1;        // 1–6
	float mixTargetLufs = -14.0f;
	bool filtersMissing = false; // zugeordnete Quellen ohne Filter
	std::vector<SourceView> sources;
	std::array<TrackView, kTrackCount> tracks;
	std::vector<Warning> warnings;
	std::vector<HistoryEntry> history; // neueste zuerst
	uint64_t historyVersion = 0;
};

// Misst alle Audioquellen und Tonspuren und passt im Modus „Regeln“ ihre Pegel über den Filter
// „Auto Mixer“ an. Hält Quellen nur über schwache Referenzen und meldet alle Callbacks beim Entfernen und
// Beenden wieder ab. Läuft in einem eigenen Thread mit 20 Hz; alle öffentlichen Methoden sind threadsicher.
class Engine {
public:
	Engine();
	~Engine();
	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	void start();   // nach dem Laden von OBS: Quellen einsammeln, Thread starten
	void stop();    // beim Beenden: Thread anhalten, alle Callbacks abmelden, Referenzen loslassen
	void suspend(); // Szenensammlung wird gewechselt: alle Quellen loslassen
	void resume();  // Szenensammlung geladen: Quellen neu einsammeln

	EngineView view() const;
	Settings settings() const;
	void setSettings(const Settings &settings);

	void setEnabled(bool enabled);
	void setMode(Mode mode);
	void selectPreset(const std::string &id);
	void setRole(const std::string &uuid, dsp::Role role);
	void setLocked(const std::string &uuid, bool locked);

	// Presets: mitgelieferte und eigene (ohne „Automatisch“)
	std::vector<dsp::Preset> presets() const;
	std::vector<dsp::Preset> userPresets() const;
	void setUserPresets(const std::vector<dsp::Preset> &presets);
	dsp::Preset activePreset() const;

	std::vector<KnownSource> knownSources() const;

	// Filter „Auto Mixer“ an alle Quellen mit Rolle hängen (nur im UI-Thread aufrufen). Liefert die Namen
	// der Quellen, die einen Filter bekommen haben bzw. bekommen würden (dryRun).
	std::vector<std::string> attachFilters(bool dryRun = false);
	int removeAllFilters(); // aus allen Quellen entfernen, schaltet auf „Nur beobachten“

	// Spuren, auf denen gerade gestreamt wird (Bitmaske), 0 = nicht live
	void setStreamMixers(uint32_t mixers);
	// Szene im Programm (für die Zuordnung Szene → Preset)
	void setCurrentScene(const std::string &scene);

	// Eintrag im Verlauf des Docks
	void addHistory(const std::string &text);

	struct Entry;
	struct CaptureTap;
	struct TrackTap;

private:
	void run();
	void tick(double dt);
	void pushHistory(const std::string &text);
	void syncSources();
	void addSource(obs_source_t *source);
	void releaseEntry(Entry &entry);
	void retireTap(std::unique_ptr<CaptureTap> tap);
	void collectRetired(bool all);
	void updateEntry(Entry &entry, obs_source_t *source, double dt);
	void regulate(double dt);
	void reportEvents(Entry &entry);
	void updateTracks(double dt);
	void updateWarnings(double dt);
	void buildView();
	int streamTrack() const;
	SourceSettings &sourceSettings(const Entry &entry);
	const dsp::Preset *findPreset(const std::string &id) const;
	std::string effectivePresetId() const;
	bool regulating() const { return settings_.enabled && settings_.mode == Mode::Regulate; }

	static void onSourceEvent(void *param, calldata_t *data);

	mutable std::mutex mutex_; // schützt alles außer den Ereignissen
	Settings settings_;
	std::vector<dsp::Preset> builtinPresets_;
	std::vector<dsp::Preset> userPresets_;
	dsp::Regulator regulator_;
	float blend_ = 0.0f; // 0 = nichts anwenden, 1 = Regelung voll aktiv (weicher Übergang)
	std::string currentScene_;

	std::map<std::string, std::unique_ptr<Entry>> entries_; // nach UUID
	std::array<std::unique_ptr<TrackTap>, kTrackCount> tracks_;
	std::array<dsp::StatusTracker, kTrackCount> trackStatus_;
	std::array<TrackView, kTrackCount> trackViews_;
	std::vector<Warning> warnings_;
	std::map<WarningKind, double> warningLogged_; // wann eine Warnung zuletzt im Verlauf landete
	std::deque<HistoryEntry> history_;
	uint64_t historyVersion_ = 0;
	EngineView view_;
	uint32_t streamMixers_ = 0;
	double sinceSync_ = 0.0;
	bool suspended_ = true;
	bool settingsDirty_ = false;
	double sinceSave_ = 0.0;
	std::vector<std::pair<double, std::unique_ptr<CaptureTap>>> retired_; // erst später freigeben
	double clock_ = 0.0;

	// Ereignisse aus OBS-Signalen (beliebige Threads); eigene kleine Sperre
	std::mutex eventsMutex_;
	bool sourcesDirty_ = true;

	std::thread thread_;
	std::mutex wakeMutex_;
	std::condition_variable wake_;
	std::atomic<bool> running_{false};
};

} // namespace am
