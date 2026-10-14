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

#include "text.hpp"

#include <obs-module.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace am {

const char *tr(const char *key)
{
	return obs_module_text(key);
}

std::string trf(const char *key, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, key);
	std::vsnprintf(buffer, sizeof(buffer), obs_module_text(key), args);
	va_end(args);
	return buffer;
}

std::string formatDb(float db, bool plus)
{
	// Auf 0,5 dB runden; ganze Zahlen ohne Nachkommastelle
	const float rounded = std::round(db * 2.0f) / 2.0f;
	char number[32];
	const float magnitude = std::fabs(rounded);
	if (magnitude == std::floor(magnitude))
		std::snprintf(number, sizeof(number), "%.0f", static_cast<double>(magnitude));
	else
		std::snprintf(number, sizeof(number), "%.1f", static_cast<double>(magnitude));
	std::string text = number;
	const std::string separator = tr("AutoMixer.DecimalSeparator");
	if (const size_t dot = text.find('.'); dot != std::string::npos)
		text.replace(dot, 1, separator);
	if (rounded < 0.0f)
		text = "\xE2\x88\x92" + text; // −
	else if (plus && rounded > 0.0f)
		text = "+" + text;
	return text + " dB";
}

std::string formatLufs(float lufs)
{
	if (!(lufs > -70.0f))
		return "\xE2\x80\x93"; // –
	char number[32];
	std::snprintf(number, sizeof(number), "%.1f", static_cast<double>(std::fabs(lufs)));
	std::string text = number;
	if (const size_t dot = text.find('.'); dot != std::string::npos)
		text.replace(dot, 1, tr("AutoMixer.DecimalSeparator"));
	return (lufs < 0.0f ? "\xE2\x88\x92" : "") + text + " LUFS";
}

} // namespace am
