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

#include "calibration-dialog.hpp"

#include "roles.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace am {

namespace {

constexpr double kCalibrationSeconds = 30.0;

QPointer<CalibrationDialog> g_open;

QString qtr(const char *key)
{
	return QString::fromUtf8(text(key));
}

QString hintText(uint32_t hints)
{
	QStringList lines;
	if (hints & dsp::kHintNoSignal)
		lines << qtr("AutoMixer.Calibration.Hint.NoSignal");
	if (hints & dsp::kHintTooQuiet)
		lines << qtr("AutoMixer.Calibration.Hint.TooQuiet");
	if (hints & dsp::kHintTooLoud)
		lines << qtr("AutoMixer.Calibration.Hint.TooLoud");
	if (hints & dsp::kHintClipping)
		lines << qtr("AutoMixer.Calibration.Hint.Clipping");
	if (hints & dsp::kHintNoisy)
		lines << qtr("AutoMixer.Calibration.Hint.Noisy");
	return lines.isEmpty() ? qtr("AutoMixer.Calibration.Hint.Ok") : lines.join(QStringLiteral(" "));
}

} // namespace

void CalibrationDialog::open(Engine *engine, QWidget *parent)
{
	if (!g_open) {
		g_open = new CalibrationDialog(engine, parent);
		g_open->setAttribute(Qt::WA_DeleteOnClose);
	}
	g_open->show();
	g_open->raise();
	g_open->activateWindow();
}

CalibrationDialog::CalibrationDialog(Engine *engine, QWidget *parent) : QDialog(parent), engine_(engine)
{
	setWindowTitle(qtr("AutoMixer.Calibration.Title"));
	resize(640, 440);
	auto *layout = new QVBoxLayout(this);

	intro_ = new QLabel(
		QString::fromStdString(textf("AutoMixer.Calibration.Intro", static_cast<int>(kCalibrationSeconds))));
	intro_->setWordWrap(true);
	layout->addWidget(intro_);

	progress_ = new QProgressBar;
	progress_->setRange(0, 1000);
	progress_->setTextVisible(false);
	layout->addWidget(progress_);
	status_ = new QLabel;
	layout->addWidget(status_);

	results_ = new QTableWidget(0, 5);
	results_->setHorizontalHeaderLabels(
		{qtr("AutoMixer.Settings.Roles.Source"), qtr("AutoMixer.Settings.Roles.Role"),
		 qtr("AutoMixer.Calibration.Measured"), qtr("AutoMixer.Calibration.Suggested"),
		 qtr("AutoMixer.Calibration.Hints")});
	results_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	results_->verticalHeader()->hide();
	results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	results_->setWordWrap(true);
	results_->hide();
	layout->addWidget(results_, 1);

	preset_ = new QLabel;
	preset_->setWordWrap(true);
	preset_->hide();
	layout->addWidget(preset_);
	usePreset_ = new QCheckBox(qtr("AutoMixer.Calibration.UsePreset"));
	usePreset_->setChecked(true);
	usePreset_->hide();
	layout->addWidget(usePreset_);

	auto *buttons = new QHBoxLayout;
	start_ = new QPushButton(qtr("AutoMixer.Calibration.Start"));
	start_->setDefault(true);
	connect(start_, &QPushButton::clicked, this, &CalibrationDialog::start);
	apply_ = new QPushButton(qtr("AutoMixer.Calibration.Apply"));
	apply_->setEnabled(false);
	connect(apply_, &QPushButton::clicked, this, &CalibrationDialog::apply);
	auto *close = new QPushButton(qtr("AutoMixer.Calibration.Close"));
	connect(close, &QPushButton::clicked, this, &CalibrationDialog::reject);
	buttons->addWidget(start_);
	buttons->addStretch();
	buttons->addWidget(apply_);
	buttons->addWidget(close);
	layout->addLayout(buttons);
	layout->addStretch();

	connect(&timer_, &QTimer::timeout, this, &CalibrationDialog::poll);
}

void CalibrationDialog::reject()
{
	timer_.stop();
	if (engine_->calibration().running)
		engine_->cancelCalibration();
	QDialog::reject();
}

void CalibrationDialog::start()
{
	results_->hide();
	preset_->hide();
	usePreset_->hide();
	apply_->setEnabled(false);
	start_->setEnabled(false);
	engine_->startCalibration(kCalibrationSeconds);
	timer_.start(200);
	poll();
}

void CalibrationDialog::poll()
{
	const CalibrationView view = engine_->calibration();
	progress_->setValue(static_cast<int>(view.progress * 1000.0f));
	if (view.running) {
		status_->setText(QString::fromStdString(
			textf("AutoMixer.Calibration.Running", static_cast<int>(std::ceil(view.remaining)))));
		return;
	}
	timer_.stop();
	start_->setEnabled(true);
	start_->setText(qtr("AutoMixer.Calibration.Again"));
	if (view.finished)
		showResults(view);
}

void CalibrationDialog::showResults(const CalibrationView &view)
{
	status_->setText(qtr("AutoMixer.Calibration.Done"));
	results_->setRowCount(static_cast<int>(view.sources.size()));
	bool any = false;
	for (int row = 0; row < static_cast<int>(view.sources.size()); row++) {
		const CalibrationSource &source = view.sources[static_cast<size_t>(row)];
		const dsp::CalibrationResult &result = source.result;
		results_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(source.name)));
		results_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(roleName(source.role))));
		results_->setItem(row, 2,
				  new QTableWidgetItem(result.enoughData
							       ? QString::fromStdString(formatLufs(result.measuredLufs))
							       : QStringLiteral("–")));
		results_->setItem(row, 3,
				  new QTableWidgetItem(
					  result.enoughData ? QString::fromStdString(formatDb(result.suggestedGainDb))
							    : QStringLiteral("–")));
		auto *hints = new QTableWidgetItem(hintText(result.hints));
		hints->setToolTip(hints->text());
		results_->setItem(row, 4, hints);
		any = any || result.enoughData;
	}
	results_->resizeColumnsToContents();
	results_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	results_->resizeRowsToContents();
	results_->show();

	suggestedPreset_ = view.suggestedPreset;
	if (!suggestedPreset_.empty()) {
		QString name = QString::fromStdString(suggestedPreset_);
		for (const dsp::Preset &preset : engine_->presets()) {
			if (preset.id == suggestedPreset_)
				name = QString::fromStdString(preset.name);
		}
		preset_->setText(
			QString::fromStdString(textf("AutoMixer.Calibration.Preset", name.toUtf8().constData())));
		preset_->show();
		usePreset_->show();
	}
	apply_->setEnabled(any);
}

void CalibrationDialog::apply()
{
	engine_->applyCalibration(usePreset_->isVisible() && usePreset_->isChecked());
	accept();
}

} // namespace am
