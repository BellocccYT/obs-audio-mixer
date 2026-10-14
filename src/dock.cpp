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

#include "dock.hpp"

#include "level-meter.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>

namespace am {

namespace {

constexpr int kNameWidth = 110;
constexpr int kValueWidth = 64;

QString qtr(const char *key)
{
	return QString::fromUtf8(tr(key));
}

QString statusText(dsp::Status status)
{
	switch (status) {
	case dsp::Status::Ok:
		return qtr("AutoMixer.Status.Ok");
	case dsp::Status::TooLoud:
		return qtr("AutoMixer.Status.TooLoud");
	case dsp::Status::TooQuiet:
		return qtr("AutoMixer.Status.TooQuiet");
	case dsp::Status::Clipping:
		return qtr("AutoMixer.Status.Clipping");
	default:
		return qtr("AutoMixer.Status.Inactive");
	}
}

void setStatus(QLabel *label, dsp::Status status)
{
	label->setText(QStringLiteral("<span style=\"color:%1\">●</span>").arg(statusColor(status).name()));
	label->setToolTip(statusText(status));
}

void setElided(QLabel *label, const QString &text)
{
	label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, kNameWidth));
	label->setToolTip(text);
}

QLabel *sectionTitle(const char *key)
{
	auto *label = new QLabel(qtr(key));
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	return label;
}

} // namespace

MixerDock::MixerDock(Engine *engine, QWidget *parent) : QWidget(parent), engine_(engine)
{
	auto *layout = new QVBoxLayout(this);

	auto *header = new QHBoxLayout;
	enabled_ = new QCheckBox(qtr("AutoMixer.Dock.Enabled"));
	enabled_->setToolTip(qtr("AutoMixer.Dock.Enabled.Tip"));
	connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
		Settings settings = engine_->settings();
		settings.enabled = on;
		engine_->setSettings(settings);
	});
	mode_ = new QLabel;
	header->addWidget(enabled_);
	header->addStretch();
	header->addWidget(mode_);
	layout->addLayout(header);

	warnings_ = new QLabel;
	warnings_->setWordWrap(true);
	warnings_->setTextFormat(Qt::RichText);
	warnings_->setStyleSheet("QLabel { background: rgba(229, 62, 62, 45); border-left: 3px solid #e53e3e; "
				 "border-radius: 3px; padding: 6px; }");
	warnings_->hide();
	layout->addWidget(warnings_);

	layout->addWidget(sectionTitle("AutoMixer.Dock.Tracks"));
	tracksBox_ = new QWidget;
	tracksLayout_ = new QVBoxLayout(tracksBox_);
	tracksLayout_->setContentsMargins(0, 0, 0, 0);
	tracksLayout_->setSpacing(2);
	layout->addWidget(tracksBox_);

	layout->addWidget(sectionTitle("AutoMixer.Dock.Sources"));
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	sourcesBox_ = new QWidget;
	sourcesLayout_ = new QVBoxLayout(sourcesBox_);
	sourcesLayout_->setContentsMargins(0, 0, 0, 0);
	sourcesLayout_->setSpacing(2);
	noSources_ = new QLabel(qtr("AutoMixer.Dock.NoSources"));
	noSources_->setWordWrap(true);
	sourcesLayout_->addWidget(noSources_);
	sourcesLayout_->addStretch();
	scroll->setWidget(sourcesBox_);
	layout->addWidget(scroll, 3);

	layout->addWidget(sectionTitle("AutoMixer.Dock.History"));
	history_ = new QListWidget;
	history_->setWordWrap(true);
	history_->setMaximumHeight(140);
	layout->addWidget(history_, 1);

	connect(&timer_, &QTimer::timeout, this, &MixerDock::refresh);
	timer_.start(100);
	refresh();
}

DockRow MixerDock::makeRow(QWidget *parent)
{
	DockRow row;
	row.widget = new QWidget(parent);
	auto *layout = new QHBoxLayout(row.widget);
	layout->setContentsMargins(0, 1, 0, 1);
	row.name = new QLabel;
	row.name->setFixedWidth(kNameWidth);
	row.meter = new LevelMeter;
	row.value = new QLabel;
	row.value->setFixedWidth(kValueWidth);
	row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	row.status = new QLabel;
	row.status->setTextFormat(Qt::RichText);
	layout->addWidget(row.name);
	layout->addWidget(row.meter, 1);
	layout->addWidget(row.value);
	layout->addWidget(row.status);
	return row;
}

void MixerDock::refresh()
{
	const EngineView view = engine_->view();

	const QSignalBlocker blocker(enabled_);
	enabled_->setChecked(view.enabled);
	mode_->setText(view.mode == Mode::Regulate ? qtr("AutoMixer.Mode.Regulate") : qtr("AutoMixer.Mode.Observe"));

	QStringList warnings;
	for (const Warning &warning : view.warnings)
		warnings << QString::fromStdString(warning.text).toHtmlEscaped();
	warnings_->setVisible(!warnings.isEmpty());
	warnings_->setText(QStringLiteral("⚠ ") + warnings.join(QStringLiteral("<br>⚠ ")));

	updateTracks(view);
	updateSources(view);
	updateHistory(view);
}

void MixerDock::updateTracks(const EngineView &view)
{
	for (int i = 0; i < kTrackCount; i++) {
		const TrackView &track = view.tracks[static_cast<size_t>(i)];
		const bool stream = i + 1 == view.streamTrack;
		// Die Streamspur immer zeigen, die anderen nur, wenn auf ihnen etwas ankommt
		const bool visible = stream || track.present;
		auto it = trackRows_.find(i);
		if (!visible) {
			if (it != trackRows_.end())
				it->second.widget->hide();
			continue;
		}
		if (it == trackRows_.end()) {
			it = trackRows_.emplace(i, makeRow(tracksBox_)).first;
			tracksLayout_->insertWidget(std::min(i, tracksLayout_->count()), it->second.widget);
		}
		DockRow &row = it->second;
		row.widget->show();
		const QString name = stream ? QString::fromStdString(trf("AutoMixer.Dock.StreamTrack", i + 1))
					    : QString::fromStdString(trf("AutoMixer.Dock.Track", i + 1));
		setElided(row.name, name);
		row.meter->setLevels(track.momentary, track.shortTerm, track.truePeak, view.mixTargetLufs, track.status,
				     false);
		row.value->setText(QString::fromStdString(formatLufs(track.shortTerm)));
		setStatus(row.status, track.status);
	}
}

void MixerDock::updateSources(const EngineView &view)
{
	std::vector<std::string> order;
	for (const SourceView &source : view.sources)
		order.push_back(source.uuid);

	// Zeilen verschwundener Quellen entfernen
	for (auto it = sourceRows_.begin(); it != sourceRows_.end();) {
		if (std::find(order.begin(), order.end(), it->first) == order.end()) {
			delete it->second.widget;
			it = sourceRows_.erase(it);
		} else {
			++it;
		}
	}
	for (const SourceView &source : view.sources) {
		if (sourceRows_.find(source.uuid) == sourceRows_.end())
			sourceRows_.emplace(source.uuid, makeRow(sourcesBox_));
	}
	// Reihenfolge neu aufbauen, wenn sie sich geändert hat
	if (order != sourceOrder_) {
		for (const std::string &uuid : order)
			sourcesLayout_->removeWidget(sourceRows_[uuid].widget);
		int index = 1; // nach dem Hinweis „keine Quellen“
		for (const std::string &uuid : order)
			sourcesLayout_->insertWidget(index++, sourceRows_[uuid].widget);
		sourceOrder_ = order;
	}
	noSources_->setVisible(view.sources.empty());

	for (const SourceView &source : view.sources) {
		DockRow &row = sourceRows_[source.uuid];
		setElided(row.name, QString::fromStdString(source.name));
		row.meter->setLevels(source.momentary, source.shortTerm, source.truePeak, source.targetLufs,
				     source.status, !source.inProgram || source.muted);
		row.value->setText(source.hasFilter ? QString::fromStdString(formatDb(source.gainDb))
						    : QStringLiteral("–"));
		row.value->setToolTip(
			qtr(source.hasFilter ? "AutoMixer.Dock.Gain.Tip" : "AutoMixer.Dock.NoFilter.Tip"));
		setStatus(row.status, source.status);
		if (source.muted)
			row.status->setToolTip(qtr("AutoMixer.Status.Muted"));
		else if (!source.inProgram)
			row.status->setToolTip(qtr("AutoMixer.Status.NotInProgram"));
	}
}

void MixerDock::updateHistory(const EngineView &view)
{
	if (view.historyVersion == historyVersion_)
		return;
	historyVersion_ = view.historyVersion;
	history_->clear();
	for (const HistoryEntry &entry : view.history) {
		const QString time = QDateTime::fromSecsSinceEpoch(entry.time).toString(QStringLiteral("HH:mm:ss"));
		history_->addItem(time + QStringLiteral("  ") + QString::fromStdString(entry.text));
	}
}

} // namespace am
