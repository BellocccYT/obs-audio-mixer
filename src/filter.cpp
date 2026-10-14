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

#include "filter.hpp"

#include "dsp/output-stage.hpp"

#include <obs-module.h>

#include <algorithm>
#include <cstring>

namespace am {

namespace {

constexpr int kMaxChannels = 8;

// Eine Instanz des Filters. Alle Puffer werden beim Anlegen reserviert; im Audio-Thread wird nichts
// angefordert, nichts geloggt und nichts gesperrt.
struct FilterInstance {
	obs_source_t *source = nullptr;
	std::shared_ptr<FilterLink> link = std::make_shared<FilterLink>();
	dsp::Analyzer analyzer;
	dsp::OutputStage stage;
	size_t channels = 2;
};

const char *filterName(void *)
{
	return obs_module_text("AutoMixer.Filter");
}

void *filterCreate(obs_data_t *, obs_source_t *source)
{
	auto *filter = new FilterInstance();
	filter->source = source;
	const size_t channels = audio_output_get_channels(obs_get_audio());
	const double rate = static_cast<double>(audio_output_get_sample_rate(obs_get_audio()));
	filter->channels = std::clamp<size_t>(channels, 1, kMaxChannels);
	filter->analyzer.configure(rate, static_cast<int>(filter->channels));
	dsp::OutputStage::Config config;
	config.sampleRate = rate;
	config.channels = static_cast<int>(filter->channels);
	filter->stage.configure(config);
	return filter;
}

void filterDestroy(void *data)
{
	delete static_cast<FilterInstance *>(data);
}

obs_audio_data *filterAudio(void *data, obs_audio_data *audio)
{
	auto *filter = static_cast<FilterInstance *>(data);
	FilterLink &link = *filter->link;
	float *planes[kMaxChannels] = {};
	for (size_t c = 0; c < filter->channels; c++)
		planes[c] = reinterpret_cast<float *>(audio->data[c]);

	// Messen vor der eigenen Verstärkung
	filter->analyzer.process(planes, audio->frames);
	if (filter->analyzer.takeNewBlock())
		link.input.write(filter->analyzer.snapshot());

	const bool bypass = link.bypass.load(std::memory_order_relaxed);
	const float gain = bypass ? 0.0f : link.gainDb.load(std::memory_order_relaxed);
	const bool limiter = !bypass && link.limiter.load(std::memory_order_relaxed);
	filter->stage.process(planes, audio->frames, gain, limiter, link.ceilingDb.load(std::memory_order_relaxed));

	link.appliedGainDb.store(filter->stage.gainDb(), std::memory_order_relaxed);
	const float reduction = filter->stage.gainReductionDb();
	float previous = link.limiterDb.load(std::memory_order_relaxed);
	while (reduction < previous &&
	       !link.limiterDb.compare_exchange_weak(previous, reduction, std::memory_order_relaxed)) {
	}
	link.blocks.fetch_add(1, std::memory_order_relaxed);
	return audio;
}

obs_properties_t *filterProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *info =
		obs_properties_add_text(props, "info", obs_module_text("AutoMixer.Filter.Info"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(info, true);
	return props;
}

} // namespace

void registerFilter()
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = filterName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.filter_audio = filterAudio;
	info.get_properties = filterProperties;
	obs_register_source(&info);
}

std::shared_ptr<FilterLink> filterLink(obs_source_t *filter)
{
	if (!filter || std::strcmp(obs_source_get_unversioned_id(filter), kFilterId) != 0)
		return {};
	auto *instance = static_cast<FilterInstance *>(obs_obj_get_data(filter));
	return instance ? instance->link : nullptr;
}

} // namespace am
