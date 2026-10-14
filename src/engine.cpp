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
#include "presets.hpp"
#include "roles.hpp"
#include "text.hpp"

#include "dsp/analyzer.hpp"
#include "dsp/triple-buffer.hpp"

// obs-module.h muss vor plugin-support.h stehen, sonst kollidiert die blogva-Deklaration
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>

namespace am {

namespace {

constexpr double kTickSeconds = 0.05; // Regeltakt 20 Hz
constexpr double kSyncSeconds = 2.0;  // Quellenliste spätestens so oft abgleichen
constexpr double kFilterScanSeconds = 0.5;
constexpr double kFreshSeconds = 0.6;    // so lange gelten Messwerte als aktuell
constexpr double kRetireSeconds = 5.0;   // abgemeldete Messpunkte erst so spät freigeben
constexpr double kVisibleSeconds = 30.0; // so lange nach dem letzten Ton bleibt eine Quelle im Dock
constexpr double kSaveSeconds = 5.0;     // geänderte Einstellungen spätestens so oft speichern
constexpr double kBlendSeconds = 2.0;    // weicher Übergang beim Ein- und Ausschalten der Regelung
constexpr double kDuckReportSeconds = 20.0;
constexpr double kWarningRepeatSeconds = 30.0; // dieselbe Warnung frühestens nach so langer Ruhe erneut
constexpr float kMutedSpeechSeconds = 1.5f;    // so lange sprechen bei stummem Mikro, bis gewarnt wird
constexpr float kFaderMoveDb = 0.05f;
constexpr size_t kMaxHistory = 100;
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

bool hasMixerFilter(obs_source_t *source)
{
	bool found = false;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (std::strcmp(obs_source_get_unversioned_id(filter), kFilterId) == 0)
				*static_cast<bool *>(param) = true;
		},
		&found);
	return found;
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
	dsp::Role suggested = dsp::Role::Ignore;
	OBSWeakSourceAutoRelease weak;
	std::unique_ptr<CaptureTap> tap;

	std::shared_ptr<FilterLink> filter;
	bool filterEnabled = false;
	double sinceFilterScan = 1e9;

	dsp::AnalyzerSnapshot tapSnapshot;
	dsp::AnalyzerSnapshot filterSnapshot;
	Freshness tapFresh;
	Freshness filterFresh;
	float applied = 0.0f;     // Verstärkung, die der Filter gerade anwendet
	float limited = 0.0f;     // Absenkung durch den Limiter seit dem letzten Schritt
	bool reallyMuted = false; // stummgeschaltet (nicht nur Push-to-Talk)

	dsp::Channel channel;
	dsp::StatusTracker status;
	double lastSound = -1e9; // Zeitpunkt des letzten hörbaren Tons
	double lastDuckReport = -1e9;
	float mutedSpeaking = 0.0f;
	float lastFaderDb = 0.0f;
	bool faderKnown = false;
	dsp::CalibrationCollector collector;
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
		builtinPresets_ = loadBuiltinPresets();
		userPresets_ = loadUserPresets();
		// Ohne Zustimmung nie regeln (z. B. wenn die Einstellungen von Hand geändert wurden)
		if (!settings_.filterConsent)
			settings_.mode = Mode::Observe;
		autoSelector_.reset(dsp::preset_ids::kVoiceFirst);
		lastEffective_ = effectivePresetId();

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
	for (const char *signal :
	     {"source_create", "source_remove", "source_rename", "source_filter_add", "source_filter_remove"})
		signal_handler_connect(signals, signal, onSourceEvent, this);
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
	for (const char *signal :
	     {"source_create", "source_remove", "source_rename", "source_filter_add", "source_filter_remove"})
		signal_handler_disconnect(signals, signal, onSourceEvent, this);

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
	if (!settings_.filterConsent)
		settings_.mode = Mode::Observe;
	saveSettings(settings_);
	settingsDirty_ = false;
}

void Engine::setEnabled(bool enabled)
{
	std::lock_guard lock(mutex_);
	if (settings_.enabled == enabled)
		return;
	settings_.enabled = enabled;
	pushHistory(text(enabled ? "AutoMixer.History.Enabled" : "AutoMixer.History.Paused"));
	settingsDirty_ = true;
}

void Engine::setMode(Mode mode)
{
	std::lock_guard lock(mutex_);
	if (mode == Mode::Regulate && !settings_.filterConsent)
		return; // erst nach Zustimmung (attachFilters)
	if (settings_.mode == mode)
		return;
	settings_.mode = mode;
	pushHistory(textf("AutoMixer.History.Mode",
			  text(mode == Mode::Regulate ? "AutoMixer.Mode.Regulate" : "AutoMixer.Mode.Observe")));
	settingsDirty_ = true;
}

void Engine::selectPreset(const std::string &id)
{
	std::lock_guard lock(mutex_);
	if (settings_.preset == id)
		return;
	settings_.preset = id;
	const dsp::Preset *preset = findPreset(effectivePresetId());
	if (id == "auto")
		pushHistory(text("AutoMixer.History.PresetAuto"));
	else if (preset)
		pushHistory(textf("AutoMixer.History.Preset", preset->name.c_str()));
	settingsDirty_ = true;
}

void Engine::setRole(const std::string &uuid, dsp::Role role)
{
	std::lock_guard lock(mutex_);
	auto it = entries_.find(uuid);
	SourceSettings *source = it != entries_.end() ? &sourceSettings(*it->second) : settings_.findSource(uuid, "");
	if (!source)
		return;
	source->role = role;
	source->roleChosen = true;
	settingsDirty_ = true;
}

void Engine::setLocked(const std::string &uuid, bool locked)
{
	std::lock_guard lock(mutex_);
	auto it = entries_.find(uuid);
	SourceSettings *source = it != entries_.end() ? &sourceSettings(*it->second) : settings_.findSource(uuid, "");
	if (!source || source->locked == locked)
		return;
	source->locked = locked;
	pushHistory(textf(locked ? "AutoMixer.History.Locked" : "AutoMixer.History.Unlocked", source->name.c_str()));
	settingsDirty_ = true;
}

std::vector<dsp::Preset> Engine::presets() const
{
	std::lock_guard lock(mutex_);
	std::vector<dsp::Preset> all = builtinPresets_;
	all.insert(all.end(), userPresets_.begin(), userPresets_.end());
	return all;
}

std::vector<dsp::Preset> Engine::userPresets() const
{
	std::lock_guard lock(mutex_);
	return userPresets_;
}

void Engine::setUserPresets(const std::vector<dsp::Preset> &presets)
{
	std::lock_guard lock(mutex_);
	userPresets_ = presets;
	for (dsp::Preset &preset : userPresets_)
		preset.builtin = false;
	saveUserPresets(userPresets_);
}

dsp::Preset Engine::activePreset() const
{
	std::lock_guard lock(mutex_);
	const dsp::Preset *preset = findPreset(effectivePresetId());
	return preset ? *preset : fallbackPreset();
}

std::vector<KnownSource> Engine::knownSources() const
{
	std::lock_guard lock(mutex_);
	std::vector<KnownSource> list;
	for (const auto &[uuid, entry] : entries_) {
		KnownSource known;
		known.uuid = uuid;
		known.name = entry->name;
		known.type = entry->type;
		known.suggested = entry->suggested;
		known.hasFilter = entry->filter != nullptr;
		known.role = entry->channel.in.role;
		known.locked = entry->channel.in.locked;
		known.minGainDb = entry->channel.in.minGainDb;
		known.maxGainDb = entry->channel.in.maxGainDb;
		list.push_back(known);
	}
	std::sort(list.begin(), list.end(),
		  [](const KnownSource &a, const KnownSource &b) { return lowercase(a.name) < lowercase(b.name); });
	return list;
}

std::vector<std::string> Engine::attachFilters(bool dryRun)
{
	std::lock_guard lock(mutex_);
	std::vector<std::string> names;
	for (auto &[uuid, entry] : entries_) {
		if (sourceSettings(*entry).role == dsp::Role::Ignore)
			continue;
		OBSSourceAutoRelease source = obs_weak_source_get_source(entry->weak);
		if (!source || hasMixerFilter(source))
			continue;
		names.push_back(entry->name);
		if (dryRun)
			continue;
		// Ans Ende der Filterkette: OBS fügt neue Filter dort ein
		OBSSourceAutoRelease filter = obs_source_create_private(kFilterId, text("AutoMixer.Filter"), nullptr);
		if (filter)
			obs_source_filter_add(source, filter);
		entry->sinceFilterScan = 1e9;
	}
	if (!dryRun) {
		settings_.filterConsent = true;
		settingsDirty_ = true;
		if (!names.empty()) {
			pushHistory(textf("AutoMixer.History.FiltersAdded", static_cast<int>(names.size())));
			obs_log(LOG_INFO, "Filter an %d Quellen angehängt", static_cast<int>(names.size()));
		}
	}
	return names;
}

int Engine::removeAllFilters()
{
	std::lock_guard lock(mutex_);
	std::vector<std::pair<OBSSource, OBSSource>> found; // Quelle, Filter
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			obs_source_enum_filters(
				source,
				[](obs_source_t *parent, obs_source_t *filter, void *list) {
					if (std::strcmp(obs_source_get_unversioned_id(filter), kFilterId) == 0)
						static_cast<std::vector<std::pair<OBSSource, OBSSource>> *>(list)
							->emplace_back(parent, filter);
				},
				param);
			return true;
		},
		&found);
	for (auto &[source, filter] : found)
		obs_source_filter_remove(source, filter);
	for (auto &[uuid, entry] : entries_) {
		if (entry->filter)
			entry->filter->bypass = true;
		entry->filter.reset();
		entry->sinceFilterScan = 1e9;
	}
	settings_.mode = Mode::Observe;
	settings_.filterConsent = false;
	settingsDirty_ = true;
	pushHistory(textf("AutoMixer.History.FiltersRemoved", static_cast<int>(found.size())));
	obs_log(LOG_INFO, "Filter von %d Quellen entfernt", static_cast<int>(found.size()));
	return static_cast<int>(found.size());
}

void Engine::relearn()
{
	std::lock_guard lock(mutex_);
	for (auto &[uuid, entry] : entries_)
		entry->channel.state.resetLearning();
	pushHistory(text("AutoMixer.History.Relearn"));
}

void Engine::startCalibration(double seconds)
{
	std::lock_guard lock(mutex_);
	calibrating_ = true;
	calibrationFinished_ = false;
	calibrationElapsed_ = 0.0;
	calibrationDuration_ = std::max(5.0, seconds);
	dsp::AutoPresetSelector::Config config;
	config.windowSeconds = static_cast<float>(calibrationDuration_);
	config.warmupSeconds = 0.0f;
	calibrationActivity_.setConfig(config);
	calibrationActivity_.reset();
	for (auto &[uuid, entry] : entries_)
		entry->collector.reset();
	pushHistory(text("AutoMixer.History.CalibrationStarted"));
}

void Engine::cancelCalibration()
{
	std::lock_guard lock(mutex_);
	calibrating_ = false;
	calibrationFinished_ = false;
}

CalibrationView Engine::calibration() const
{
	std::lock_guard lock(mutex_);
	CalibrationView view;
	view.running = calibrating_;
	view.finished = calibrationFinished_;
	view.progress = static_cast<float>(std::clamp(calibrationElapsed_ / calibrationDuration_, 0.0, 1.0));
	view.remaining = std::max(0.0, calibrationDuration_ - calibrationElapsed_);
	if (!calibrationFinished_)
		return view;
	for (const auto &[uuid, entry] : entries_) {
		const dsp::ChannelInput &in = entry->channel.in;
		if (in.role == dsp::Role::Ignore)
			continue;
		CalibrationSource source;
		source.uuid = uuid;
		source.name = entry->name;
		source.role = in.role;
		source.targetLufs = regulator_.target(in.role);
		source.result = entry->collector.result(source.targetLufs, in);
		view.sources.push_back(source);
	}
	std::sort(view.sources.begin(), view.sources.end(), [](const CalibrationSource &a, const CalibrationSource &b) {
		if (a.role != b.role)
			return static_cast<int>(a.role) < static_cast<int>(b.role);
		return lowercase(a.name) < lowercase(b.name);
	});
	view.suggestedPreset = calibrationActivity_.suggestion();
	return view;
}

void Engine::applyCalibration(bool usePreset)
{
	std::lock_guard lock(mutex_);
	if (!calibrationFinished_)
		return;
	int applied = 0;
	for (auto &[uuid, entry] : entries_) {
		const dsp::ChannelInput &in = entry->channel.in;
		if (in.role == dsp::Role::Ignore || in.locked)
			continue;
		const dsp::CalibrationResult result = entry->collector.result(regulator_.target(in.role), in);
		if (!result.enoughData)
			continue;
		dsp::CalibrationCollector::apply(result, entry->channel.state);
		applied++;
	}
	const std::string suggested = calibrationActivity_.suggestion();
	if (usePreset && !suggested.empty() && presetExists(suggested)) {
		settings_.preset = suggested;
		settingsDirty_ = true;
	}
	calibrationFinished_ = false;
	pushHistory(textf("AutoMixer.History.CalibrationApplied", applied));
}

void Engine::setStreamMixers(uint32_t mixers)
{
	std::lock_guard lock(mutex_);
	streamMixers_ = mixers;
}

void Engine::setCurrentScene(const std::string &scene)
{
	std::lock_guard lock(mutex_);
	currentScene_ = scene;
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

	updateAutoPreset(dt);
	updateCalibration(dt);
	regulate(dt);
	updateTracks(dt);
	updateWarnings(dt);
	buildView();

	sinceSave_ += dt;
	if (settingsDirty_ && sinceSave_ >= kSaveSeconds) {
		saveSettings(settings_);
		settingsDirty_ = false;
		sinceSave_ = 0.0;
	}
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
	entry->suggested = suggestRole(source);
	entry->weak = obs_source_get_weak_source(source);

	entry->tap = std::make_unique<CaptureTap>();
	entry->tap->channels = audioChannels();
	entry->tap->analyzer.configure(audioRate(), entry->tap->channels);
	obs_source_add_audio_capture_callback(source, CaptureTap::callback, entry->tap.get());

	sourceSettings(*entry); // legt die Einstellungen mit der vorgeschlagenen Rolle an
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

SourceSettings &Engine::sourceSettings(const Entry &entry)
{
	SourceSettings *source = settings_.findSource(entry.uuid, entry.name);
	if (!source) {
		SourceSettings created;
		created.uuid = entry.uuid;
		created.name = entry.name;
		created.role = entry.suggested;
		created.minGainDb = settings_.defaultMinGainDb;
		created.maxGainDb = settings_.defaultMaxGainDb;
		settings_.sources.push_back(created);
		settingsDirty_ = true;
		return settings_.sources.back();
	}
	// Umbenannt oder per Name wiedergefunden (neue UUID): festhalten
	if (source->uuid != entry.uuid || source->name != entry.name) {
		source->uuid = entry.uuid;
		source->name = entry.name;
		settingsDirty_ = true;
	}
	return *source;
}

const dsp::Preset *Engine::findPreset(const std::string &id) const
{
	for (const dsp::Preset &preset : builtinPresets_) {
		if (preset.id == id)
			return &preset;
	}
	for (const dsp::Preset &preset : userPresets_) {
		if (preset.id == id)
			return &preset;
	}
	return builtinPresets_.empty() ? nullptr : &builtinPresets_.front();
}

bool Engine::presetExists(const std::string &id) const
{
	const auto matches = [&](const dsp::Preset &preset) {
		return preset.id == id;
	};
	return std::any_of(builtinPresets_.begin(), builtinPresets_.end(), matches) ||
	       std::any_of(userPresets_.begin(), userPresets_.end(), matches);
}

std::string Engine::effectivePresetId() const
{
	if (settings_.preset != dsp::preset_ids::kAuto)
		return settings_.preset;
	// „Automatisch“: erst die Zuordnung Szene → Preset, dann die Aktivität
	if (const ScenePreset *mapped = settings_.findScene(currentScene_); mapped && presetExists(mapped->preset))
		return mapped->preset;
	if (settings_.autoByActivity && presetExists(autoSelector_.current()))
		return autoSelector_.current();
	return dsp::preset_ids::kVoiceFirst;
}

dsp::Activity Engine::activity() const
{
	dsp::Activity activity;
	for (const auto &[uuid, entry] : entries_) {
		const dsp::ChannelInput &in = entry->channel.in;
		if (!in.audible)
			continue;
		const bool sounding = in.momentary > -45.0f;
		activity.game = activity.game ||
				((in.role == dsp::Role::Game || in.role == dsp::Role::Desktop) && sounding);
		activity.music = activity.music || (in.role == dsp::Role::Music && sounding);
		activity.voice = activity.voice || (in.role == dsp::Role::Voice && in.speaking);
		activity.chat = activity.chat || (in.role == dsp::Role::Chat && in.speaking);
	}
	return activity;
}

void Engine::updateAutoPreset(double dt)
{
	// Die Aktivität wird immer mitgeschrieben, damit „Automatisch“ beim Einschalten schon Bescheid weiß
	if (settings_.enabled)
		autoSelector_.update(static_cast<float>(dt), activity());
	const std::string effective = effectivePresetId();
	if (effective == lastEffective_)
		return;
	lastEffective_ = effective;
	if (settings_.preset != dsp::preset_ids::kAuto)
		return;
	const dsp::Preset *preset = findPreset(effective);
	const ScenePreset *mapped = settings_.findScene(currentScene_);
	const std::string reason = mapped && mapped->preset == effective
					   ? textf("AutoMixer.Auto.Reason.Scene", currentScene_.c_str())
					   : std::string(text(("AutoMixer.Auto.Reason." + effective).c_str()));
	pushHistory(textf("AutoMixer.History.AutoPreset", preset ? preset->name.c_str() : effective.c_str(),
			  reason.c_str()));
}

void Engine::updateCalibration(double dt)
{
	if (!calibrating_)
		return;
	for (auto &[uuid, entry] : entries_)
		entry->collector.add(static_cast<float>(dt), entry->channel.in);
	calibrationActivity_.update(static_cast<float>(dt), activity());
	calibrationElapsed_ += dt;
	if (calibrationElapsed_ >= calibrationDuration_) {
		calibrating_ = false;
		calibrationFinished_ = true;
		pushHistory(text("AutoMixer.History.CalibrationDone"));
	}
}

void Engine::updateEntry(Entry &entry, obs_source_t *source, double dt)
{
	entry.name = obs_source_get_name(source);
	const SourceSettings config = sourceSettings(entry);

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
	entry.applied = filterFresh ? entry.filter->appliedGainDb.load(std::memory_order_relaxed) : 0.0f;
	entry.limited = filterFresh ? entry.filter->limiterDb.exchange(0.0f, std::memory_order_relaxed) : 0.0f;

	// Zustand in OBS
	const float faderDb = faderToDb(obs_source_get_volume(source));
	entry.reallyMuted = obs_source_muted(source);
	const bool muted = entry.reallyMuted || entry.tap->muted.load(std::memory_order_relaxed);
	const bool inProgram = obs_source_active(source);
	const bool inMix = obs_source_get_audio_mixers(source) != 0 &&
			   obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_MONITOR_ONLY;
	const bool audible = present && inProgram && inMix && !muted && faderDb > -90.0f;

	// Fader bewegt? Dann gehört die Quelle dem Nutzer, bis er sie wieder freigibt
	bool locked = config.locked;
	if (!entry.faderKnown) {
		entry.lastFaderDb = faderDb;
		entry.faderKnown = true;
	} else if (std::fabs(faderDb - entry.lastFaderDb) > kFaderMoveDb) {
		entry.lastFaderDb = faderDb;
		if (settings_.lockOnFader && regulating() && config.role != dsp::Role::Ignore && !locked &&
		    entry.filter) {
			sourceSettings(entry).locked = true;
			locked = true;
			settingsDirty_ = true;
			pushHistory(textf("AutoMixer.History.LockedByFader", entry.name.c_str()));
		}
	}

	dsp::ChannelInput &in = entry.channel.in;
	in.role = config.role;
	in.locked = locked;
	in.minGainDb = config.minGainDb;
	in.maxGainDb = config.maxGainDb;
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
	const float offset = entry.applied + faderDb;
	view.uuid = entry.uuid;
	view.name = entry.name;
	view.type = entry.type;
	view.role = config.role;
	view.locked = locked;
	view.hasFilter = entry.filter && entry.filterEnabled;
	view.inProgram = inProgram;
	view.muted = muted;
	view.present = present;
	view.speaking = in.speaking;
	view.momentary = present ? offsetLevel(snap.momentary, offset) : dsp::kSilence;
	view.shortTerm = present ? offsetLevel(snap.shortTerm, offset) : dsp::kSilence;
	view.truePeak = present ? offsetLevel(snap.truePeak, offset + entry.limited) : dsp::kSilence;
	view.gainDb = entry.applied;
	view.faderDb = faderDb;
	if (present && snap.momentary > kActiveLufs)
		entry.lastSound = clock_;
}

void Engine::regulate(double dt)
{
	const dsp::Preset *preset = findPreset(effectivePresetId());
	regulator_.setPreset(preset ? *preset : fallbackPreset());
	dsp::RegulatorConfig config = regulator_.config();
	config.referenceLufs = settings_.voiceTargetLufs;
	config.ceilingDbtp = settings_.ceilingDbtp;
	regulator_.setConfig(config);

	std::vector<dsp::Channel *> channels;
	channels.reserve(entries_.size());
	for (auto &[uuid, entry] : entries_)
		channels.push_back(&entry->channel);
	regulator_.update(dt, channels.data(), channels.size());

	// Weicher Übergang beim Ein- und Ausschalten: nie ein Sprung im Pegel
	const float step = static_cast<float>(dt / kBlendSeconds);
	blend_ = regulating() ? std::min(1.0f, blend_ + step) : std::max(0.0f, blend_ - step);

	for (auto &[uuid, entry] : entries_) {
		const dsp::ChannelState &state = entry->channel.state;
		if (entry->filter) {
			entry->filter->gainDb.store(state.gainDb * blend_, std::memory_order_relaxed);
			entry->filter->ceilingDb.store(settings_.ceilingDbtp, std::memory_order_relaxed);
			entry->filter->limiter.store(true, std::memory_order_relaxed);
			entry->filter->bypass.store(blend_ <= 0.0f, std::memory_order_relaxed);
		}
		reportEvents(*entry);

		// Status gegen das Ziel; Ducking und Schreischutz senken das Ziel bewusst ab
		SourceView &view = entry->view;
		const dsp::ChannelInput &in = entry->channel.in;
		const bool speech = dsp::isSpeechRole(in.role);
		const float offset = entry->applied + view.faderDb;
		const float level = speech && !dsp::isSilent(in.speech) ? offsetLevel(in.speech, offset)
									: view.shortTerm;
		const bool active = in.audible && (speech ? in.speaking : view.momentary > kActiveLufs);
		const float target = dsp::isSilent(state.targetLufs)
					     ? dsp::kSilence
					     : state.targetLufs + (state.duckGain + state.guardGain) * blend_;
		entry->status.setConfig(settings_.warnings);
		view.targetLufs = state.targetLufs;
		view.suggestedGainDb = state.gainDb;
		view.status = entry->status.update(static_cast<float>(dt), active, level, target, view.truePeak, false);
	}
}

void Engine::reportEvents(Entry &entry)
{
	const dsp::ChannelState &state = entry.channel.state;
	if (!state.events)
		return;
	const bool applied = blend_ > 0.5f && entry.filter;
	const char *name = entry.name.c_str();
	if ((state.events & dsp::kEventDuckStart) && applied && clock_ - entry.lastDuckReport > kDuckReportSeconds) {
		// Wer spricht: du (Stimme) oder jemand im Chat?
		const bool you = std::any_of(entries_.begin(), entries_.end(), [](const auto &item) {
			const dsp::ChannelInput &in = item.second->channel.in;
			return in.role == dsp::Role::Voice && in.speaking && in.audible;
		});
		pushHistory(textf(you ? "AutoMixer.History.Ducking" : "AutoMixer.History.DuckingChat", name,
				  formatDb(std::fabs(state.eventDb), false).c_str()));
		entry.lastDuckReport = clock_;
	}
	if ((state.events & dsp::kEventGuard) && applied)
		pushHistory(textf("AutoMixer.History.Guard", name, formatDb(std::fabs(state.eventDb), false).c_str()));
	if (state.events & (dsp::kEventLevelDown | dsp::kEventLevelUp)) {
		const bool down = state.events & dsp::kEventLevelDown;
		const char *key = applied ? (down ? "AutoMixer.History.LevelDown" : "AutoMixer.History.LevelUp")
					  : (down ? "AutoMixer.History.SuggestDown" : "AutoMixer.History.SuggestUp");
		pushHistory(textf(key, name, formatDb(std::fabs(state.eventDb), false).c_str()));
	}
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

void Engine::updateWarnings(double dt)
{
	std::vector<Warning> warnings;
	const int track = streamTrack();
	const TrackView &stream = trackViews_[static_cast<size_t>(track - 1)];
	if (stream.status == dsp::Status::TooLoud)
		warnings.push_back({WarningKind::MixTooLoud, textf("AutoMixer.Warning.MixTooLoud", track,
								   formatLufs(stream.shortTerm).c_str())});
	if (stream.status == dsp::Status::Clipping)
		warnings.push_back({WarningKind::MixClipping, textf("AutoMixer.Warning.MixClipping", track)});

	// „Du sprichst, aber dein Mikro ist stumm“: stummgeschaltet (nicht Push-to-Talk) und trotzdem Sprache
	bool desktopActive = false;
	bool separated = false;
	for (auto &[uuid, entry] : entries_) {
		const dsp::ChannelInput &in = entry->channel.in;
		if (in.role == dsp::Role::Voice) {
			const bool speakingMuted = entry->reallyMuted && in.speaking;
			entry->mutedSpeaking =
				speakingMuted ? entry->mutedSpeaking + static_cast<float>(dt)
					      : std::max(0.0f, entry->mutedSpeaking - 2.0f * static_cast<float>(dt));
			if (settings_.muteWarning && entry->mutedSpeaking >= kMutedSpeechSeconds)
				warnings.push_back({WarningKind::MicMuted,
						    textf("AutoMixer.Warning.MicMuted", entry->name.c_str())});
		}
		const bool recent = clock_ - entry->lastSound < 60.0;
		if (in.role == dsp::Role::Desktop && in.audible && clock_ - entry->lastSound < 10.0)
			desktopActive = true;
		if ((in.role == dsp::Role::Game || in.role == dsp::Role::Chat || in.role == dsp::Role::Music) && recent)
			separated = true;
	}
	// Läuft alles über Desktop-Audio, lassen sich Spiel, Musik und Discord nicht trennen
	if (desktopActive && !separated && !settings_.desktopHintDismissed)
		warnings.push_back({WarningKind::DesktopOnly, text("AutoMixer.Warning.DesktopOnly"), false});

	// Neue Warnungen auch im Verlauf festhalten, aber eine wiederkehrende nicht jedes Mal
	for (const Warning &warning : warnings) {
		if (!warning.severe)
			continue;
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
	view_.activePresetId = effectivePresetId();
	view_.streamTrack = streamTrack();
	view_.mixTargetLufs = settings_.mixTargetLufs;
	view_.tracks = trackViews_;
	view_.warnings = warnings_;

	view_.sources.clear();
	view_.filtersMissing = false;
	for (const auto &[uuid, entry] : entries_) {
		const dsp::ChannelInput &in = entry->channel.in;
		const bool recent = clock_ - entry->lastSound <= kVisibleSeconds;
		if (in.role != dsp::Role::Ignore && !entry->filter && recent)
			view_.filtersMissing = true;
		if (recent)
			view_.sources.push_back(entry->view);
	}
	// Erst nach Rolle (Stimme oben), dann nach Name
	std::sort(view_.sources.begin(), view_.sources.end(), [](const SourceView &a, const SourceView &b) {
		if (a.role != b.role)
			return static_cast<int>(a.role) < static_cast<int>(b.role);
		return lowercase(a.name) < lowercase(b.name);
	});

	if (view_.historyVersion != historyVersion_) {
		view_.history.assign(history_.begin(), history_.end());
		view_.historyVersion = historyVersion_;
	}
}

void Engine::onSourceEvent(void *param, calldata_t *)
{
	auto *engine = static_cast<Engine *>(param);
	std::lock_guard lock(engine->eventsMutex_);
	engine->sourcesDirty_ = true;
}

} // namespace am
