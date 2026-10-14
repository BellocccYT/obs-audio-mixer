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

#include "dsp/status.hpp"

#include <QColor>
#include <QWidget>

namespace am {

// Farbe zum Status einer Quelle oder Spur
QColor statusColor(dsp::Status status);

// Waagerechter Pegelbalken: Short-term-Lautheit als Balken, Momentary als heller Strich, True Peak als
// kleine Spitzenmarke und das Ziel des Presets als senkrechte Marke
class LevelMeter : public QWidget {
	Q_OBJECT

public:
	explicit LevelMeter(QWidget *parent = nullptr);

	void setLevels(float momentary, float shortTerm, float truePeak, float target, dsp::Status status, bool dimmed);

	QSize sizeHint() const override { return QSize(120, 14); }
	QSize minimumSizeHint() const override { return QSize(40, 10); }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	float position(float db) const; // 0–1 auf der Skala

	float momentary_;
	float shortTerm_;
	float truePeak_;
	float target_;
	dsp::Status status_ = dsp::Status::Inactive;
	bool dimmed_ = false;
};

} // namespace am
