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

#include "level-meter.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace am {

namespace {

constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 0.0f;

} // namespace

QColor statusColor(dsp::Status status)
{
	switch (status) {
	case dsp::Status::Ok:
		return QColor(0x3f, 0xb9, 0x50);
	case dsp::Status::TooLoud:
		return QColor(0xe3, 0x8b, 0x08);
	case dsp::Status::TooQuiet:
		return QColor(0x3a, 0x8e, 0xe6);
	case dsp::Status::Clipping:
		return QColor(0xe5, 0x3e, 0x3e);
	default:
		return QColor(0x80, 0x80, 0x80);
	}
}

LevelMeter::LevelMeter(QWidget *parent)
	: QWidget(parent),
	  momentary_(dsp::kSilence),
	  shortTerm_(dsp::kSilence),
	  truePeak_(dsp::kSilence),
	  target_(dsp::kSilence)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LevelMeter::setLevels(float momentary, float shortTerm, float truePeak, float target, dsp::Status status,
			   bool dimmed)
{
	momentary_ = momentary;
	shortTerm_ = shortTerm;
	truePeak_ = truePeak;
	target_ = target;
	status_ = status;
	dimmed_ = dimmed;
	update();
}

float LevelMeter::position(float db) const
{
	return std::clamp((db - kMinDb) / (kMaxDb - kMinDb), 0.0f, 1.0f);
}

void LevelMeter::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal width = area.width();

	QPainterPath frame;
	frame.addRoundedRect(area, 3, 3);
	painter.fillPath(frame, palette().color(QPalette::Base).darker(115));
	painter.setClipPath(frame);

	// Short-term als Balken in der Statusfarbe
	QColor bar = status_ == dsp::Status::Inactive ? QColor(0x3f, 0xb9, 0x50) : statusColor(status_);
	if (dimmed_)
		bar.setAlpha(90);
	if (!dsp::isSilent(shortTerm_))
		painter.fillRect(QRectF(area.left(), area.top(), width * position(shortTerm_), area.height()), bar);

	// Momentary als heller Strich
	if (!dsp::isSilent(momentary_)) {
		const qreal x = area.left() + width * position(momentary_);
		QColor light = bar.lighter(150);
		painter.fillRect(QRectF(x - 1.5, area.top(), 3, area.height()), light);
	}

	// True Peak als kleine Marke oben
	if (!dsp::isSilent(truePeak_)) {
		const qreal x = area.left() + width * position(truePeak_);
		painter.fillRect(QRectF(x - 1, area.top(), 2, area.height() * 0.4),
				 truePeak_ >= -1.0f ? QColor(0xe5, 0x3e, 0x3e) : palette().color(QPalette::Text));
	}

	// Ziel des Presets
	if (!dsp::isSilent(target_)) {
		const qreal x = area.left() + width * position(target_);
		painter.setPen(QPen(palette().color(QPalette::Text), 2));
		painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
	}
}

} // namespace am
