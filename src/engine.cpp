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

#include "engine.hpp"

#include "filter.hpp"
#include "text.hpp"

#include "dsp/analyzer.hpp"
#include "dsp/regulator.hpp"
#include "dsp/triple-buffer.hpp"

// obs-module.h muss vor plugin-support.h stehen, sonst kollidiert die blogva-Deklaration
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>

namespace am {

namespace {

constexpr double kTickSeconds = 0.05; // Regeltakt 20 Hz
constexpr double kSyncSeconds = 2.0;  // Quellenliste spätestens so oft abgleichen
constexpr double kFilterScanSeconds = 0.5;
constexpr double kFreshSeconds = 0.6;    // so lange gelten Messwerte als aktuell
constexpr double kRetireSeconds = 5.0;   // abgemeldete Messpunkte erst so spät freigeben
constexpr double kVisibleSeconds = 30.0; // so lange nach dem letzten Ton bleibt eine Quelle im Dock
constexpr size_t kMaxHistory = 100;
constexpr double kWarningRepeatSeconds = 30.0; // dieselbe Warnung frühestens nach so langer Ruhe erneut
constexpr int kMaxChannels = 8;
constexpr float kActiveLufs = -50.0f;

float faderToDb(float volume)
{
	return volume > 1e-5f ? 20.0f * std::log10(volume) : -100.0f;
}

float offsetLevel(float value, float offset)
{
	return dsp::isSilent(value) ? dsp::kSilence : std::max(value + offset, dsp::kSilence);
}

std::string lowercase(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

bool isAudioInput(obs_source_t *source)
{
	return obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
	       (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

std::string sourceUuid(obs_source_t *source)
{
	const char *uuid = obs_source_get_uuid(source);
	return uuid ? uuid : "";
}

int audioChannels()
{
	return std::clamp(static_cast<int>(audio_output_get_channels(obs_get_audio())), 1, kMaxChannels);
}

double audioRate()
{
	return static_cast<double>(audio_output_get_sample_rate(obs_get_audio()));
}

// Merkt sich, ob Messwerte nachkommen (die Blockzahl ändert sich)
struct Freshness {
	uint64_t blocks = 0;
	double changed = -1e9;

	bool update(uint64_t now, double clock)
	{
		if (now != blocks) {
			blocks = now;
			changed = clock;
		}
		return blocks > 0 && clock - changed < kFreshSeconds;
	}
};

} // namespace

// Messpunkt nach allen Filtern, vor dem Fader (Capture-Callback). Läuft im Audio-Thread der Quelle: nur
// Atomics und der sperrfreie Puffer, keine Speicheranforderung.
struct Engine::CaptureTap {
	dsp::Analyzer analyzer;
	dsp::TripleBuffer<dsp::AnalyzerSnapshot> out;
	std::atomic<bool> analyze{true}; // aus, solange der Auto-Mixer-Filter selbst misst
	std::atomic<bool> muted{false};  // stumm oder Push-to-Talk nicht gedrückt
	int channels = 2;
	bool idle = false; // nur im Audio-Thread benutzt

	static void callback(void *param, obs_source_t *, const audio_data *audio, bool muted)
	{
		auto *tap = static_cast<CaptureTap *>(param);
		tap->muted.store(muted, std::memory_order_relaxed);
		if (!tap->analyze.load(std::memory_order_relaxed)) {
			tap->idle = true;
			return;
		}
		if (tap->idle) {
			tap->analyzer.reset(); // alte Werte aus der Zeit vor der Pause verwerfen
			tap->idle = false;
		}
		const float *planes[kMaxChannels] = {};
		for (int c = 0; c < tap->channels; c++)
			planes[c] = reinterpret_cast<const float *>(audio->data[c]);
		tap->analyzer.process(planes, audio->frames);
		if (tap->analyzer.takeNewBlock())
			tap->out.write(tap->analyzer.snapshot());
	}
};

// Messpunkt auf einer OBS-Tonspur: das, was wirklich im Stream oder in der Aufnahme landet (nach dem Fader)
struct Engine::TrackTap {
	dsp::Analyzer analyzer;
	dsp::TripleBuffer<dsp::AnalyzerSnapshot> out;
	int channels = 2;
	dsp::AnalyzerSnapshot snapshot; // nur im Regel-Thread
	Freshness fresh;
	uint64_t lastClipped = 0;

	static void callback(void *param, size_t, audio_data *data)
	{
		auto *tap = static_cast<TrackTap *>(param);
		const float *planes[kMaxChannels] = {};
		for (int c = 0; c < tap->channels; c++)
			planes[c] = reinterpret_cast<const float *>(data->data[c]);
		tap->analyzer.process(planes, data->frames);
		if (tap->analyzer.takeNewBlock())
			tap->out.write(tap->analyzer.snapshot());
	}
};

struct Engine::Entry {
	std::string uuid;
	std::string name;
	std::string type;
	OBSWeakSourceAutoRelease weak;
	std::unique_ptr<CaptureTap> tap;

	std::shared_ptr<FilterLink> filter;
	bool filterEnabled = false;
	double sinceFilterScan = 1e9;

	dsp::AnalyzerSnapshot tapSnapshot;
	dsp::AnalyzerSnapshot filterSnapshot;
	Freshness tapFresh;
	Freshness filterFresh;

	dsp::Channel channel;
	dsp::StatusTracker status;
	double lastSound = -1e9; // Zeitpunkt des letzten hörbaren Tons
	SourceView view;
};

Engine::Engine() = default;

Engine::~Engine()
{
	stop();
	// Erst jetzt ist sicher, dass OBS keine abgemeldeten Messpunkte mehr aufruft
	retired_.clear();
}

void Engine::start()
{
	if (running_)
		return;
	{
		std::lock_guard lock(mutex_);
		settings_ = loadSettings();

		obs_audio_info info = {};
		obs_get_audio_info(&info);
		audio_convert_info conversion = {};
		conversion.samples_per_sec = info.samples_per_sec;
		conversion.format = AUDIO_FORMAT_FLOAT_PLANAR;
		conversion.speakers = info.speakers;
		for (int i = 0; i < kTrackCount; i++) {
			auto tap = std::make_unique<TrackTap>();
			tap->channels = audioChannels();
			dsp::Analyzer::Config config;
			config.vad = false;
			tap->analyzer.configure(audioRate(), tap->channels, config);
			obs_add_raw_audio_callback(static_cast<size_t>(i), &conversion, TrackTap::callback, tap.get());
			tracks_[static_cast<size_t>(i)] = std::move(tap);
		}
		suspended_ = false;
	}

	signal_handler_t *signals = obs_get_signal_handler();
	signal_handler_connect(signals, "source_create", onSourceCreate, this);
	signal_handler_connect(signals, "source_remove", onSourceRemove, this);
	signal_handler_connect(signals, "source_rename", onSourceRename, this);
	signal_handler_connect(signals, "source_filter_add", onFilterChange, this);
	signal_handler_connect(signals, "source_filter_remove", onFilterChange, this);
	{
		std::lock_guard lock(eventsMutex_);
		sourcesDirty_ = true;
	}

	running_ = true;
	thread_ = std::thread(&Engine::run, this);
	obs_log(LOG_INFO, "Messung gestartet");
}

void Engine::stop()
{
	if (!running_ && !thread_.joinable())
		return;
	running_ = false;
	wake_.notify_all();
	if (thread_.joinable())
		thread_.join();

	signal_handler_t *signals = obs_get_signal_handler();
	signal_handler_disconnect(signals, "source_create", onSourceCreate, this);
	signal_handler_disconnect(signals, "source_remove", onSourceRemove, this);
	signal_handler_disconnect(signals, "source_rename", onSourceRename, this);
	signal_handler_disconnect(signals, "source_filter_add", onFilterChange, this);
	signal_handler_disconnect(signals, "source_filter_remove", onFilterChange, this);

	std::lock_guard lock(mutex_);
	for (auto &[uuid, entry] : entries_)
		releaseEntry(*entry);
	entries_.clear();
	for (int i = 0; i < kTrackCount; i++) {
		auto &tap = tracks_[static_cast<size_t>(i)];
		if (tap) {
			obs_remove_raw_audio_callback(static_cast<size_t>(i), TrackTap::callback, tap.get());
			tap.reset();
		}
	}
	suspended_ = true;
	saveSettings(settings_);
	obs_log(LOG_INFO, "Messung beendet");
}

void Engine::suspend()
{
	std::lock_guard lock(mutex_);
	for (auto &[uuid, entry] : entries_)
		releaseEntry(*entry);
	entries_.clear();
	suspended_ = true;
}

void Engine::resume()
{
	{
		std::lock_guard lock(mutex_);
		suspended_ = false;
	}
	std::lock_guard lock(eventsMutex_);
	sourcesDirty_ = true;
}

EngineView Engine::view() const
{
	std::lock_guard lock(mutex_);
	return view_;
}

Settings Engine::settings() const
{
	std::lock_guard lock(mutex_);
	return settings_;
}

void Engine::setSettings(const Settings &settings)
{
	std::lock_guard lock(mutex_);
	settings_ = settings;
	saveSettings(settings_);
}

void Engine::setStreamMixers(uint32_t mixers)
{
	std::lock_guard lock(mutex_);
	streamMixers_ = mixers;
}

void Engine::addHistory(const std::string &text)
{
	std::lock_guard lock(mutex_);
	pushHistory(text);
}

void Engine::pushHistory(const std::string &text)
{
	HistoryEntry entry;
	entry.time = static_cast<int64_t>(std::time(nullptr));
	entry.text = text;
	history_.push_front(entry);
	while (history_.size() > kMaxHistory)
		history_.pop_back();
	historyVersion_++;
}

void Engine::run()
{
	using Clock = std::chrono::steady_clock;
	auto last = Clock::now();
	while (running_) {
		{
			std::unique_lock lock(wakeMutex_);
			wake_.wait_for(lock, std::chrono::duration<double>(kTickSeconds), [this] { return !running_; });
		}
		if (!running_)
			break;
		const auto now = Clock::now();
		const double dt = std::chrono::duration<double>(now - last).count();
		last = now;
		tick(std::min(dt, 0.5));
	}
}

void Engine::tick(double dt)
{
	std::lock_guard lock(mutex_);
	clock_ += dt;
	collectRetired(false);
	if (suspended_) {
		buildView();
		return;
	}

	bool dirty;
	{
		std::lock_guard events(eventsMutex_);
		dirty = sourcesDirty_;
		sourcesDirty_ = false;
	}
	sinceSync_ += dt;
	if (dirty || sinceSync_ >= kSyncSeconds) {
		syncSources();
		sinceSync_ = 0.0;
	}

	for (auto it = entries_.begin(); it != entries_.end();) {
		Entry &entry = *it->second;
		OBSSourceAutoRelease source = obs_weak_source_get_source(entry.weak);
		if (!source || obs_source_removed(source)) {
			releaseEntry(entry);
			it = entries_.erase(it);
			continue;
		}
		updateEntry(entry, source, dt);
		++it;
	}

	updateTracks(dt);
	updateWarnings();
	buildView();
}

void Engine::syncSources()
{
	std::vector<OBSSource> sources;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (isAudioInput(source))
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
			return true;
		},
		&sources);
	for (const OBSSource &source : sources) {
		if (!source || obs_source_removed(source))
			continue;
		if (entries_.find(sourceUuid(source)) == entries_.end())
			addSource(source);
	}
}

void Engine::addSource(obs_source_t *source)
{
	auto entry = std::make_unique<Entry>();
	entry->uuid = sourceUuid(source);
	if (entry->uuid.empty())
		return;
	entry->name = obs_source_get_name(source);
	entry->type = obs_source_get_unversioned_id(source);
	entry->weak = obs_source_get_weak_source(source);

	entry->tap = std::make_unique<CaptureTap>();
	entry->tap->channels = audioChannels();
	entry->tap->analyzer.configure(audioRate(), entry->tap->channels);
	obs_source_add_audio_capture_callback(source, CaptureTap::callback, entry->tap.get());

	const std::string uuid = entry->uuid;
	entries_[uuid] = std::move(entry);
}

void Engine::releaseEntry(Entry &entry)
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(entry.weak);
	if (entry.filter) {
		// Filter in den neutralen Zustand: 0 dB, kein Limiter
		entry.filter->bypass = true;
		entry.filter.reset();
	}
	if (!entry.tap)
		return;
	if (source) {
		// Nach dem Abmelden ruft OBS den Callback garantiert nicht mehr auf
		obs_source_remove_audio_capture_callback(source, CaptureTap::callback, entry.tap.get());
		entry.tap.reset();
	} else {
		// Die Quelle wird gerade zerstört: OBS meldet den Callback selbst ab, aber vielleicht nicht sofort
		retireTap(std::move(entry.tap));
	}
}

void Engine::retireTap(std::unique_ptr<CaptureTap> tap)
{
	retired_.emplace_back(clock_, std::move(tap));
}

void Engine::collectRetired(bool all)
{
	retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
				      [&](const auto &item) { return all || clock_ - item.first > kRetireSeconds; }),
		       retired_.end());
}

void Engine::updateEntry(Entry &entry, obs_source_t *source, double dt)
{
	const float step = static_cast<float>(dt);
	entry.name = obs_source_get_name(source);

	// Auto-Mixer-Filter suchen (er misst dann selbst, vor seiner Verstärkung)
	entry.sinceFilterScan += dt;
	if (entry.sinceFilterScan >= kFilterScanSeconds) {
		entry.sinceFilterScan = 0.0;
		struct Found {
			std::shared_ptr<FilterLink> link;
			bool enabled = false;
		} found;
		obs_source_enum_filters(
			source,
			[](obs_source_t *, obs_source_t *filter, void *param) {
				if (auto link = filterLink(filter)) {
					auto *result = static_cast<Found *>(param);
					result->link = link;
					result->enabled = obs_source_enabled(filter);
				}
			},
			&found);
		if (entry.filter && entry.filter != found.link)
			entry.filter->bypass = true;
		entry.filter = found.link;
		entry.filterEnabled = found.enabled;
	}

	// Messwerte: vom Filter, wenn er läuft, sonst vom Capture-Callback
	entry.tap->out.read(entry.tapSnapshot);
	const bool tapFresh = entry.tapFresh.update(entry.tapSnapshot.blocks, clock_);
	bool filterFresh = false;
	if (entry.filter && entry.filterEnabled) {
		entry.filter->input.read(entry.filterSnapshot);
		filterFresh = entry.filterFresh.update(entry.filterSnapshot.blocks, clock_);
	}
	entry.tap->analyze = !filterFresh;
	const dsp::AnalyzerSnapshot &snap = filterFresh ? entry.filterSnapshot : entry.tapSnapshot;
	const bool present = filterFresh || tapFresh;
	const float applied = filterFresh ? entry.filter->appliedGainDb.load(std::memory_order_relaxed) : 0.0f;
	const float limited = filterFresh ? entry.filter->limiterDb.exchange(0.0f, std::memory_order_relaxed) : 0.0f;

	// Zustand in OBS
	const float faderDb = faderToDb(obs_source_get_volume(source));
	const bool muted = obs_source_muted(source) || entry.tap->muted.load(std::memory_order_relaxed);
	const bool inProgram = obs_source_active(source);
	const bool inMix = obs_source_get_audio_mixers(source) != 0 &&
			   obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_MONITOR_ONLY;
	const bool audible = present && inProgram && inMix && !muted && faderDb > -90.0f;

	dsp::ChannelInput &in = entry.channel.in;
	in.present = present;
	in.audible = audible;
	in.block = snap.block;
	in.momentary = snap.momentary;
	in.shortTerm = snap.shortTerm;
	in.speech = snap.speech;
	in.speechBlock = snap.speechBlock;
	in.truePeak = snap.truePeak;
	in.noiseFloor = snap.noiseFloor;
	in.speaking = present && snap.speaking;
	in.faderDb = std::max(faderDb, -60.0f);

	// So kommt es im Mix an: Eingang + Anpassung + Fader
	SourceView &view = entry.view;
	const float offset = applied + faderDb;
	view.uuid = entry.uuid;
	view.name = entry.name;
	view.type = entry.type;
	view.hasFilter = entry.filter && entry.filterEnabled;
	view.inProgram = inProgram;
	view.muted = muted;
	view.present = present;
	view.speaking = in.speaking;
	view.momentary = present ? offsetLevel(snap.momentary, offset) : dsp::kSilence;
	view.shortTerm = present ? offsetLevel(snap.shortTerm, offset) : dsp::kSilence;
	view.truePeak = present ? offsetLevel(snap.truePeak, offset + limited) : dsp::kSilence;
	view.gainDb = applied;
	view.faderDb = faderDb;
	if (present && snap.momentary > kActiveLufs)
		entry.lastSound = clock_;

	const bool active = audible && view.momentary > kActiveLufs;
	view.status = entry.status.update(step, active, view.shortTerm, view.targetLufs, view.truePeak, false);
}

void Engine::updateTracks(double dt)
{
	dsp::StatusConfig config = settings_.warnings;
	config.loudLu = std::max(1.0f, settings_.mixMaxLufs - settings_.mixTargetLufs);
	config.quietLu = 12.0f;
	config.quietSeconds = 10.0f;
	for (int i = 0; i < kTrackCount; i++) {
		TrackTap *tap = tracks_[static_cast<size_t>(i)].get();
		TrackView &view = trackViews_[static_cast<size_t>(i)];
		if (!tap) {
			view = TrackView();
			continue;
		}
		tap->out.read(tap->snapshot);
		const bool fresh = tap->fresh.update(tap->snapshot.blocks, clock_);
		view.present = fresh && tap->snapshot.momentary > -70.0f;
		view.momentary = fresh ? tap->snapshot.momentary : dsp::kSilence;
		view.shortTerm = fresh ? tap->snapshot.shortTerm : dsp::kSilence;
		view.truePeak = fresh ? tap->snapshot.truePeak : dsp::kSilence;
		const bool clipped = tap->snapshot.clipped > tap->lastClipped;
		tap->lastClipped = tap->snapshot.clipped;
		dsp::StatusTracker &tracker = trackStatus_[static_cast<size_t>(i)];
		tracker.setConfig(config);
		view.status = tracker.update(static_cast<float>(dt), view.momentary > kActiveLufs, view.shortTerm,
					     settings_.mixTargetLufs, view.truePeak, clipped);
	}
}

int Engine::streamTrack() const
{
	if (settings_.streamTrack >= 1 && settings_.streamTrack <= kTrackCount)
		return settings_.streamTrack;
	for (int i = 0; i < kTrackCount; i++) {
		if (streamMixers_ & (1u << i))
			return i + 1;
	}
	return 1;
}

void Engine::updateWarnings()
{
	std::vector<Warning> warnings;
	const int track = streamTrack();
	const TrackView &stream = trackViews_[static_cast<size_t>(track - 1)];
	if (stream.status == dsp::Status::TooLoud)
		warnings.push_back({WarningKind::MixTooLoud,
				    trf("AutoMixer.Warning.MixTooLoud", track, formatLufs(stream.shortTerm).c_str())});
	if (stream.status == dsp::Status::Clipping)
		warnings.push_back({WarningKind::MixClipping, trf("AutoMixer.Warning.MixClipping", track)});

	// Neue Warnungen auch im Verlauf festhalten, aber eine wiederkehrende nicht jedes Mal
	for (const Warning &warning : warnings) {
		auto logged = warningLogged_.find(warning.kind);
		if (logged == warningLogged_.end() || clock_ - logged->second > kWarningRepeatSeconds)
			pushHistory(warning.text);
		warningLogged_[warning.kind] = clock_;
	}
	warnings_ = std::move(warnings);
}

void Engine::buildView()
{
	view_.running = running_ && !suspended_;
	view_.enabled = settings_.enabled;
	view_.mode = settings_.mode;
	view_.presetId = settings_.preset;
	view_.streamTrack = streamTrack();
	view_.mixTargetLufs = settings_.mixTargetLufs;
	view_.tracks = trackViews_;
	view_.warnings = warnings_;

	view_.sources.clear();
	for (const auto &[uuid, entry] : entries_) {
		if (clock_ - entry->lastSound > kVisibleSeconds)
			continue;
		view_.sources.push_back(entry->view);
	}
	std::sort(view_.sources.begin(), view_.sources.end(),
		  [](const SourceView &a, const SourceView &b) { return lowercase(a.name) < lowercase(b.name); });

	if (view_.historyVersion != historyVersion_) {
		view_.history.assign(history_.begin(), history_.end());
		view_.historyVersion = historyVersion_;
	}
}

void Engine::onSourceCreate(void *param, calldata_t *)
{
	auto *engine = static_cast<Engine *>(param);
	std::lock_guard lock(engine->eventsMutex_);
	engine->sourcesDirty_ = true;
}

void Engine::onSourceRemove(void *param, calldata_t *)
{
	auto *engine = static_cast<Engine *>(param);
	std::lock_guard lock(engine->eventsMutex_);
	engine->sourcesDirty_ = true;
}

void Engine::onSourceRename(void *param, calldata_t *)
{
	auto *engine = static_cast<Engine *>(param);
	std::lock_guard lock(engine->eventsMutex_);
	engine->sourcesDirty_ = true;
}

void Engine::onFilterChange(void *param, calldata_t *)
{
	auto *engine = static_cast<Engine *>(param);
	std::lock_guard lock(engine->eventsMutex_);
	engine->sourcesDirty_ = true;
}

} // namespace am
