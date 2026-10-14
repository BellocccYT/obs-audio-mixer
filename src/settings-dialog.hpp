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

#include "dsp/preset.hpp"

#include <QDialog>

#include <array>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace am {

// Einstellungen: Allgemein, Rollen, Presets (Editor, Import/Export), Szenen und Warnschwellen
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	// Öffnet den Dialog (oder holt den offenen nach vorn)
	static void open(Engine *engine, QWidget *parent);

private:
	SettingsDialog(Engine *engine, QWidget *parent);

	QWidget *createGeneralTab();
	QWidget *createRolesTab();
	QWidget *createPresetsTab();
	QWidget *createScenesTab();
	QWidget *createWarningsTab();

	void load();
	void apply();

	// Preset-Editor
	void showPreset(int index);
	void storePreset();
	void addPreset(bool copyCurrent);
	void deletePreset();
	void importPresets();
	void exportPresets();
	void refreshPresetList(int select);
	dsp::Preset *editablePreset();

	Engine *engine_;
	Settings settings_;
	std::vector<dsp::Preset> builtin_;
	std::vector<dsp::Preset> user_;
	std::vector<KnownSource> sources_;

	QTabWidget *tabs_;

	// Allgemein
	QCheckBox *enabled_;
	QDoubleSpinBox *voiceTarget_;
	QDoubleSpinBox *mixTarget_;
	QDoubleSpinBox *mixMax_;
	QComboBox *streamTrack_;
	QDoubleSpinBox *ceiling_;
	QDoubleSpinBox *minGain_;
	QDoubleSpinBox *maxGain_;
	QCheckBox *lockOnFader_;
	QCheckBox *desktopHint_;

	// Rollen
	QTableWidget *roles_;

	// Presets
	QListWidget *presetList_;
	QWidget *presetEditor_;
	QLineEdit *presetName_;
	QLineEdit *presetDescription_;
	QComboBox *presetAnchor_;
	QDoubleSpinBox *presetTarget_;
	std::array<QDoubleSpinBox *, dsp::kRoleCount> presetOffsets_{};
	QDoubleSpinBox *presetDuck_;
	std::array<QCheckBox *, dsp::kRoleCount> presetDucked_{};
	std::array<QCheckBox *, dsp::kRoleCount> presetTriggers_{};
	QSpinBox *presetHold_;
	QComboBox *presetSpeed_;
	QCheckBox *presetGuard_;
	QPushButton *presetDelete_;
	QPushButton *presetExport_;
	int presetIndex_ = -1;
	bool loadingPreset_ = false;

	// Szenen
	QTableWidget *scenes_;
	QCheckBox *autoByActivity_;

	// Warnungen
	QDoubleSpinBox *warnLoud_;
	QDoubleSpinBox *warnLoudSeconds_;
	QDoubleSpinBox *warnQuiet_;
	QDoubleSpinBox *warnQuietSeconds_;
	QDoubleSpinBox *warnClip_;
	QCheckBox *muteWarning_;
};

} // namespace am
