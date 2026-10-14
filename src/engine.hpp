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

enum class WarningKind { MixTooLoud, MixClipping, MicMuted, DesktopOnly, NeedsConsent };

struct Warning {
	WarningKind kind;
	std::string text;
};

// Was das Dock über eine Quelle anzeigt
struct SourceView {
	std::string uuid;
	std::string name;
	std::string type; // Quellentyp (unversionierte ID)
	dsp::Role role = dsp::Role::Ignore;
	bool roleChosen = false;
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
	float suggestedGainDb = 0.0f; // was die Regelung tun würde (im Modus „Nur beobachten“)
	float faderDb = 0.0f;
	dsp::Status status = dsp::Status::Inactive;
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
	std::string presetId;
	int streamTrack = 1; // 1–6
	float mixTargetLufs = -14.0f;
	std::vector<SourceView> sources;
	std::array<TrackView, kTrackCount> tracks;
	std::vector<Warning> warnings;
	std::vector<HistoryEntry> history; // neueste zuerst
	uint64_t historyVersion = 0;
};

// Misst alle Audioquellen und Tonspuren und (ab Modus „Regeln“) passt ihre Pegel an. Hält Quellen nur über
// schwache Referenzen und meldet alle Callbacks beim Entfernen und Beenden wieder ab. Läuft in einem
// eigenen Thread mit 20 Hz; alle öffentlichen Methoden sind threadsicher.
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

	// Spuren, auf denen gerade gestreamt wird (Bitmaske), 0 = nicht live
	void setStreamMixers(uint32_t mixers);

	// Eintrag im Verlauf des Docks
	void addHistory(const std::string &text);

	struct Entry;
	struct CaptureTap;
	struct TrackTap;

private:
	void run();
	void tick(double dt);
	void pushHistory(const std::string &text);
	void processEvents();
	void syncSources();
	void addSource(obs_source_t *source);
	void releaseEntry(Entry &entry);
	void retireTap(std::unique_ptr<CaptureTap> tap);
	void collectRetired(bool all);
	void updateEntry(Entry &entry, obs_source_t *source, double dt);
	void updateTracks(double dt);
	void updateWarnings();
	void buildView();
	int streamTrack() const;

	static void onSourceCreate(void *param, calldata_t *data);
	static void onSourceRemove(void *param, calldata_t *data);
	static void onSourceRename(void *param, calldata_t *data);
	static void onFilterChange(void *param, calldata_t *data);

	mutable std::mutex mutex_; // schützt alles außer den Ereignissen
	Settings settings_;
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
