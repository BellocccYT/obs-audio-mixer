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

#include "calibration-dialog.hpp"
#include "level-meter.hpp"
#include "roles.hpp"
#include "settings-dialog.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace am {

namespace {

constexpr int kNameWidth = 110;
constexpr int kValueWidth = 64;

QString qtr(const char *key)
{
	return QString::fromUtf8(text(key));
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

void setStatus(QLabel *label, dsp::Status status, const QString &tip = QString())
{
	label->setText(QStringLiteral("<span style=\"color:%1\">●</span>").arg(statusColor(status).name()));
	label->setToolTip(tip.isEmpty() ? statusText(status) : tip);
}

void setElided(QLabel *label, const QString &text, int width = kNameWidth)
{
	label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, width));
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

QLabel *banner(const char *style)
{
	auto *label = new QLabel;
	label->setWordWrap(true);
	label->setTextFormat(Qt::RichText);
	label->setStyleSheet(style);
	label->hide();
	return label;
}

} // namespace

MixerDock::MixerDock(Engine *engine, QWidget *parent) : QWidget(parent), engine_(engine)
{
	auto *layout = new QVBoxLayout(this);

	// Kopf: Ein/Aus und Preset
	auto *header = new QHBoxLayout;
	enabled_ = new QCheckBox(qtr("AutoMixer.Dock.Enabled"));
	enabled_->setToolTip(qtr("AutoMixer.Dock.Enabled.Tip"));
	connect(enabled_, &QCheckBox::toggled, this, [this](bool on) { engine_->setEnabled(on); });
	preset_ = new QComboBox;
	preset_->setToolTip(qtr("AutoMixer.Dock.Preset.Tip"));
	connect(preset_, &QComboBox::activated, this, [this](int index) {
		const QString id = preset_->itemData(index).toString();
		if (!id.isEmpty())
			engine_->selectPreset(id.toStdString());
	});
	header->addWidget(enabled_);
	header->addWidget(preset_, 1);
	layout->addLayout(header);

	// Modus und Einstellungen
	auto *controls = new QHBoxLayout;
	mode_ = new QComboBox;
	mode_->addItem(qtr("AutoMixer.Mode.Regulate"), static_cast<int>(Mode::Regulate));
	mode_->addItem(qtr("AutoMixer.Mode.Observe"), static_cast<int>(Mode::Observe));
	mode_->setToolTip(qtr("AutoMixer.Dock.Mode.Tip"));
	connect(mode_, &QComboBox::activated, this, [this](int index) {
		if (static_cast<Mode>(mode_->itemData(index).toInt()) == Mode::Regulate)
			requestRegulate();
		else
			engine_->setMode(Mode::Observe);
	});
	auto *calibrate = new QPushButton(qtr("AutoMixer.Dock.Calibrate"));
	calibrate->setToolTip(qtr("AutoMixer.Dock.Calibrate.Tip"));
	connect(calibrate, &QPushButton::clicked, this, [this] { CalibrationDialog::open(engine_, this); });
	auto *settings = new QPushButton(qtr("AutoMixer.Dock.Settings"));
	connect(settings, &QPushButton::clicked, this, &MixerDock::openSettings);
	controls->addWidget(mode_, 1);
	controls->addWidget(calibrate);
	controls->addWidget(settings);
	layout->addLayout(controls);

	warnings_ = banner("QLabel { background: rgba(229, 62, 62, 45); border-left: 3px solid #e53e3e; "
			   "border-radius: 3px; padding: 6px; }");
	layout->addWidget(warnings_);
	hint_ = banner("QLabel { background: rgba(227, 160, 8, 40); border-left: 3px solid #e3a008; "
		       "border-radius: 3px; padding: 6px; }");
	hint_->setOpenExternalLinks(false);
	connect(hint_, &QLabel::linkActivated, this, [this](const QString &link) {
		if (link == QStringLiteral("dismiss")) {
			Settings s = engine_->settings();
			s.desktopHintDismissed = true;
			engine_->setSettings(s);
		}
	});
	layout->addWidget(hint_);

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
	sourcesLayout_->setSpacing(4);
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

void MixerDock::requestRegulate()
{
	const Settings settings = engine_->settings();
	const std::vector<std::string> missing = engine_->attachFilters(true);
	if (!settings.filterConsent || !missing.empty()) {
		QStringList names;
		for (const std::string &name : missing)
			names << QStringLiteral("• ") + QString::fromStdString(name).toHtmlEscaped();
		QMessageBox box(QMessageBox::Question, qtr("AutoMixer"), qtr("AutoMixer.Consent.Title"),
				QMessageBox::NoButton, this);
		box.setTextFormat(Qt::RichText);
		box.setInformativeText(
			QString::fromUtf8(text("AutoMixer.Consent.Text"))
				.arg(names.isEmpty() ? qtr("AutoMixer.Consent.None") : names.join("<br>")));
		QPushButton *yes = box.addButton(qtr("AutoMixer.Consent.Yes"), QMessageBox::AcceptRole);
		box.addButton(qtr("AutoMixer.Consent.No"), QMessageBox::RejectRole);
		box.exec();
		if (box.clickedButton() != yes) {
			refresh();
			return;
		}
		engine_->attachFilters();
	}
	engine_->setMode(Mode::Regulate);
	refresh();
}

void MixerDock::openSettings()
{
	SettingsDialog::open(engine_, this);
}

DockRow MixerDock::makeTrackRow()
{
	DockRow row;
	row.widget = new QWidget(tracksBox_);
	auto *layout = new QHBoxLayout(row.widget);
	layout->setContentsMargins(0, 1, 0, 1);
	row.name = new QLabel;
	row.name->setFixedWidth(kNameWidth);
	row.meter = new LevelMeter;
	row.value = new QLabel;
	row.value->setFixedWidth(kValueWidth + 12);
	row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	row.status = new QLabel;
	row.status->setTextFormat(Qt::RichText);
	layout->addWidget(row.name);
	layout->addWidget(row.meter, 1);
	layout->addWidget(row.value);
	layout->addWidget(row.status);
	return row;
}

DockRow MixerDock::makeSourceRow(const std::string &uuid)
{
	DockRow row;
	row.widget = new QWidget(sourcesBox_);
	auto *outer = new QVBoxLayout(row.widget);
	outer->setContentsMargins(0, 2, 0, 2);
	outer->setSpacing(1);

	// Zeile 1: Name, Rolle, Schloss
	auto *top = new QHBoxLayout;
	row.name = new QLabel;
	row.role = new QComboBox;
	for (int i = 0; i < dsp::kRoleCount; i++)
		row.role->addItem(QString::fromUtf8(roleName(static_cast<dsp::Role>(i))), i);
	row.role->setToolTip(qtr("AutoMixer.Dock.Role.Tip"));
	row.lock = new QToolButton;
	row.lock->setCheckable(true);
	row.lock->setAutoRaise(true);
	row.lock->setToolTip(qtr("AutoMixer.Dock.Lock.Tip"));
	top->addWidget(row.name, 1);
	top->addWidget(row.role);
	top->addWidget(row.lock);
	outer->addLayout(top);

	// Zeile 2: Pegel mit Zielmarke, Anpassung, Status
	auto *bottom = new QHBoxLayout;
	row.meter = new LevelMeter;
	row.value = new QLabel;
	row.value->setFixedWidth(kValueWidth);
	row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	row.status = new QLabel;
	row.status->setTextFormat(Qt::RichText);
	bottom->addWidget(row.meter, 1);
	bottom->addWidget(row.value);
	bottom->addWidget(row.status);
	outer->addLayout(bottom);

	connect(row.role, &QComboBox::activated, this, [this, uuid, combo = row.role](int index) {
		engine_->setRole(uuid, static_cast<dsp::Role>(combo->itemData(index).toInt()));
	});
	connect(row.lock, &QToolButton::clicked, this,
		[this, uuid](bool checked) { engine_->setLocked(uuid, checked); });
	return row;
}

void MixerDock::refresh()
{
	const EngineView view = engine_->view();

	{
		const QSignalBlocker blocker(enabled_);
		enabled_->setChecked(view.enabled);
	}
	if (!mode_->view()->isVisible()) {
		const QSignalBlocker blocker(mode_);
		mode_->setCurrentIndex(mode_->findData(static_cast<int>(view.mode)));
	}
	updatePresets(view);

	QStringList warnings;
	QStringList hints;
	for (const Warning &warning : view.warnings) {
		const QString text = QString::fromStdString(warning.text).toHtmlEscaped();
		if (warning.severe)
			warnings << text;
		else if (warning.kind == WarningKind::DesktopOnly)
			hints << text + QStringLiteral("<br>") + qtr("AutoMixer.Warning.DesktopOnly.Help") +
					 QStringLiteral(" <a href=\"dismiss\">") + qtr("AutoMixer.Dock.Dismiss") +
					 QStringLiteral("</a>");
		else
			hints << text;
	}
	warnings_->setVisible(!warnings.isEmpty());
	warnings_->setText(QStringLiteral("⚠ ") + warnings.join(QStringLiteral("<br>⚠ ")));
	hint_->setVisible(!hints.isEmpty());
	hint_->setText(hints.join(QStringLiteral("<br>")));

	// Im Modus „Regeln“ bekommen neue Quellen mit Rolle den Filter automatisch (Zustimmung liegt vor)
	sinceAttach_ += 0.1;
	if (view.mode == Mode::Regulate && view.filtersMissing && sinceAttach_ > 2.0) {
		sinceAttach_ = 0.0;
		engine_->attachFilters();
	}

	updateTracks(view);
	updateSources(view);
	updateHistory(view);
}

void MixerDock::updatePresets(const EngineView &view)
{
	const std::vector<dsp::Preset> presets = engine_->presets();
	std::vector<std::string> ids{dsp::preset_ids::kAuto};
	for (const dsp::Preset &preset : presets)
		ids.push_back(preset.id);
	if (ids != presetIds_) {
		const QSignalBlocker blocker(preset_);
		preset_->clear();
		preset_->addItem(qtr("AutoMixer.Preset.auto"), QString::fromUtf8(dsp::preset_ids::kAuto));
		preset_->setItemData(0, qtr("AutoMixer.Preset.auto.Description"), Qt::ToolTipRole);
		for (const dsp::Preset &preset : presets) {
			preset_->addItem(QString::fromStdString(preset.name), QString::fromStdString(preset.id));
			preset_->setItemData(preset_->count() - 1, QString::fromStdString(preset.description),
					     Qt::ToolTipRole);
		}
		presetIds_ = ids;
	}
	// Bei „Automatisch“ zeigen, welches Preset gerade wirkt
	QString autoText = qtr("AutoMixer.Preset.auto");
	if (view.presetId == dsp::preset_ids::kAuto) {
		for (const dsp::Preset &preset : presets) {
			if (preset.id == view.activePresetId)
				autoText += QStringLiteral(" (") + QString::fromStdString(preset.name) +
					    QStringLiteral(")");
		}
	}
	if (preset_->itemText(0) != autoText)
		preset_->setItemText(0, autoText);
	if (!preset_->view()->isVisible()) {
		const QSignalBlocker blocker(preset_);
		preset_->setCurrentIndex(preset_->findData(QString::fromStdString(view.presetId)));
	}
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
			it = trackRows_.emplace(i, makeTrackRow()).first;
			int index = 0;
			for (const auto &[other, row] : trackRows_) {
				if (other < i)
					index++;
			}
			tracksLayout_->insertWidget(index, it->second.widget);
		}
		DockRow &row = it->second;
		row.widget->show();
		const QString name = stream ? QString::fromStdString(textf("AutoMixer.Dock.StreamTrack", i + 1))
					    : QString::fromStdString(textf("AutoMixer.Dock.Track", i + 1));
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
			sourceRows_.emplace(source.uuid, makeSourceRow(source.uuid));
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

	const bool regulating = view.enabled && view.mode == Mode::Regulate;
	for (const SourceView &source : view.sources) {
		DockRow &row = sourceRows_[source.uuid];
		setElided(row.name, QString::fromStdString(source.name), 160);
		if (!row.role->view()->isVisible()) {
			const QSignalBlocker blocker(row.role);
			row.role->setCurrentIndex(row.role->findData(static_cast<int>(source.role)));
		}
		row.lock->setChecked(source.locked);
		row.lock->setText(source.locked ? QStringLiteral("🔒") : QStringLiteral("🔓"));
		row.meter->setLevels(source.momentary, source.shortTerm, source.truePeak, source.targetLufs,
				     source.status, !source.inProgram || source.muted);

		// Anpassung: angewendet, sonst als Vorschlag in Klammern
		if (source.role == dsp::Role::Ignore) {
			row.value->setText(QStringLiteral("–"));
			row.value->setToolTip(qtr("AutoMixer.Dock.Ignored.Tip"));
		} else if (regulating && source.hasFilter) {
			row.value->setText(QString::fromStdString(formatDb(source.gainDb)));
			row.value->setToolTip(qtr("AutoMixer.Dock.Gain.Tip"));
		} else {
			row.value->setText(QStringLiteral("(") +
					   QString::fromStdString(formatDb(source.suggestedGainDb)) +
					   QStringLiteral(")"));
			row.value->setToolTip(qtr("AutoMixer.Dock.Suggestion.Tip"));
		}

		QString tip;
		if (source.muted)
			tip = qtr("AutoMixer.Status.Muted");
		else if (!source.inProgram)
			tip = qtr("AutoMixer.Status.NotInProgram");
		else if (source.locked)
			tip = qtr("AutoMixer.Status.Locked");
		setStatus(row.status, source.status, tip);
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
