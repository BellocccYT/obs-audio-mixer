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

#include "engine.hpp"

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace am {

// Einmess-Assistent: 30 s normal sprechen und spielen, danach schlägt der Auto Mixer Startwerte vor
class CalibrationDialog : public QDialog {
	Q_OBJECT

public:
	static void open(Engine *engine, QWidget *parent);

protected:
	void reject() override;

private:
	CalibrationDialog(Engine *engine, QWidget *parent);
	void start();
	void poll();
	void showResults(const CalibrationView &view);
	void apply();

	Engine *engine_;
	QLabel *intro_;
	QProgressBar *progress_;
	QLabel *status_;
	QTableWidget *results_;
	QLabel *preset_;
	QCheckBox *usePreset_;
	QPushButton *start_;
	QPushButton *apply_;
	std::string suggestedPreset_;
	QTimer timer_;
};

} // namespace am
