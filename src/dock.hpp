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

#include <QTimer>
#include <QWidget>

#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace am {

class LevelMeter;

// Eine Zeile im Dock: Quelle oder Tonspur
struct DockRow {
	QWidget *widget = nullptr;
	QLabel *name = nullptr;
	QComboBox *role = nullptr;   // nur bei Quellen
	QToolButton *lock = nullptr; // nur bei Quellen
	LevelMeter *meter = nullptr;
	QLabel *value = nullptr;
	QLabel *status = nullptr;
};

// Das Dock „Auto Mixer“: Preset, Ein/Aus, Modus, Status pro Spur und Quelle mit Pegelbalken und Zielmarke,
// Anpassung, Schloss, Warnungen und Verlauf
class MixerDock : public QWidget {
	Q_OBJECT

public:
	explicit MixerDock(Engine *engine, QWidget *parent = nullptr);

	// Beim Beenden von OBS: nicht mehr aktualisieren
	void shutdown() { timer_.stop(); }

	// Wechselt auf „Regeln“; fragt vorher, ob der Filter angehängt werden darf
	void requestRegulate();

private:
	void refresh();
	void updatePresets(const EngineView &view);
	void updateTracks(const EngineView &view);
	void updateSources(const EngineView &view);
	void updateHistory(const EngineView &view);
	DockRow makeTrackRow();
	DockRow makeSourceRow(const std::string &uuid);
	void openSettings();

	Engine *engine_;
	QCheckBox *enabled_;
	QComboBox *preset_;
	QComboBox *mode_;
	QLabel *warnings_;
	QLabel *hint_;
	QWidget *tracksBox_;
	QVBoxLayout *tracksLayout_;
	std::map<int, DockRow> trackRows_;
	QWidget *sourcesBox_;
	QVBoxLayout *sourcesLayout_;
	QLabel *noSources_;
	std::map<std::string, DockRow> sourceRows_;
	std::vector<std::string> sourceOrder_;
	std::vector<std::string> presetIds_;
	QListWidget *history_;
	uint64_t historyVersion_ = 0;
	double sinceAttach_ = 0.0;
	QTimer timer_;
};

} // namespace am
