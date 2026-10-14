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

#include "dsp/analyzer.hpp"
#include "dsp/triple-buffer.hpp"

#include <atomic>
#include <memory>

struct obs_source;
typedef struct obs_source obs_source_t;

namespace am {

constexpr const char *kFilterId = "auto_mixer_filter";

// Verbindung zwischen einer Instanz des Audiofilters „Auto Mixer“ (Audio-Thread) und der Engine
// (Regel-Thread). Nur Atomics und ein sperrfreier Puffer; beide Seiten halten einen shared_ptr, damit
// der Zustand gültig bleibt, auch wenn OBS den Filter gerade zerstört.
struct FilterLink {
	// Engine → Filter
	std::atomic<float> gainDb{0.0f};
	std::atomic<bool> bypass{true}; // nur beobachten: 0 dB, kein Limiter
	std::atomic<bool> limiter{true};
	std::atomic<float> ceilingDb{-1.0f};

	// Filter → Engine
	dsp::TripleBuffer<dsp::AnalyzerSnapshot> input; // gemessen am Filtereingang, vor der Verstärkung
	std::atomic<float> appliedGainDb{0.0f};         // tatsächlich angewendete Verstärkung
	std::atomic<float> limiterDb{0.0f};             // stärkste Absenkung des Limiters seit dem letzten Lesen
	std::atomic<uint64_t> blocks{0};                // bearbeitete Audioabschnitte (zeigt, ob der Filter läuft)
};

void registerFilter();

// Verbindung eines Filters; filter muss eine gehaltene Referenz auf einen Auto-Mixer-Filter sein
std::shared_ptr<FilterLink> filterLink(obs_source_t *filter);

} // namespace am
