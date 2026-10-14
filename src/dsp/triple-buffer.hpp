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

#include <atomic>
#include <cstdint>

namespace am::dsp {

// Sperrfreie Übergabe des jeweils neuesten Werts von genau einem Schreiber (Audio-Thread) an genau einen
// Leser (Regel-Thread). Keiner wartet je auf den anderen; der Leser bekommt immer einen vollständigen Wert.
template<typename T> class TripleBuffer {
public:
	void write(const T &value)
	{
		buffers_[back_] = value;
		const uint8_t previous =
			middle_.exchange(static_cast<uint8_t>(back_ | kFresh), std::memory_order_acq_rel);
		back_ = previous & kIndexMask;
	}

	// Liefert den neuesten Wert; true, wenn er seit dem letzten Lesen neu ist
	bool read(T &value)
	{
		bool fresh = false;
		if (middle_.load(std::memory_order_acquire) & kFresh) {
			const uint8_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
			front_ = previous & kIndexMask;
			fresh = true;
		}
		value = buffers_[front_];
		return fresh;
	}

private:
	static constexpr uint8_t kIndexMask = 0x3;
	static constexpr uint8_t kFresh = 0x4;

	T buffers_[3] = {};
	std::atomic<uint8_t> middle_{1};
	uint8_t front_ = 0; // gehört dem Leser
	uint8_t back_ = 2;  // gehört dem Schreiber
};

} // namespace am::dsp
