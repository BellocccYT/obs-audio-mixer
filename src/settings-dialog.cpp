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

#include "settings-dialog.hpp"

#include "hotkeys.hpp"
#include "presets.hpp"
#include "roles.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <obs-frontend-api.h>

#include <algorithm>

namespace am {

namespace {

QPointer<SettingsDialog> g_open;

// Rollen, für die ein Preset Ziele festlegt (ohne „Ignorieren“)
constexpr dsp::Role kTargetRoles[] = {dsp::Role::Voice, dsp::Role::Game,   dsp::Role::Chat,
				      dsp::Role::Music, dsp::Role::Alerts, dsp::Role::Desktop};

enum RoleColumns { kColName = 0, kColType, kColRole, kColSuggested, kColMin, kColMax, kColLocked, kColCount };

QString qtr(const char *key)
{
	return QString::fromUtf8(text(key));
}

QDoubleSpinBox *spin(double min, double max, double step, const char *suffix, int decimals = 1)
{
	auto *box = new QDoubleSpinBox;
	box->setRange(min, max);
	box->setSingleStep(step);
	box->setDecimals(decimals);
	box->setSuffix(QString::fromUtf8(suffix));
	return box;
}

QLabel *note(const char *key)
{
	auto *label = new QLabel(qtr(key));
	label->setWordWrap(true);
	label->setTextFormat(Qt::RichText);
	label->setOpenExternalLinks(true);
	return label;
}

QComboBox *roleCombo(bool withIgnore)
{
	auto *combo = new QComboBox;
	for (int i = 0; i < dsp::kRoleCount; i++) {
		const auto role = static_cast<dsp::Role>(i);
		if (role == dsp::Role::Ignore && !withIgnore)
			continue;
		combo->addItem(QString::fromUtf8(roleName(role)), i);
	}
	return combo;
}

} // namespace

void SettingsDialog::open(Engine *engine, QWidget *parent)
{
	if (!g_open) {
		g_open = new SettingsDialog(engine, parent);
		g_open->setAttribute(Qt::WA_DeleteOnClose);
	}
	g_open->show();
	g_open->raise();
	g_open->activateWindow();
}

SettingsDialog::SettingsDialog(Engine *engine, QWidget *parent) : QDialog(parent), engine_(engine)
{
	setWindowTitle(qtr("AutoMixer.Settings.Title"));
	resize(720, 560);
	auto *layout = new QVBoxLayout(this);
	tabs_ = new QTabWidget;
	tabs_->addTab(createGeneralTab(), qtr("AutoMixer.Settings.General"));
	tabs_->addTab(createRolesTab(), qtr("AutoMixer.Settings.Roles"));
	tabs_->addTab(createPresetsTab(), qtr("AutoMixer.Settings.Presets"));
	tabs_->addTab(createScenesTab(), qtr("AutoMixer.Settings.Scenes"));
	tabs_->addTab(createWarningsTab(), qtr("AutoMixer.Settings.Warnings"));
	layout->addWidget(tabs_);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		apply();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
	layout->addWidget(buttons);

	load();
}

QWidget *SettingsDialog::createGeneralTab()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	auto *form = new QFormLayout;

	enabled_ = new QCheckBox(qtr("AutoMixer.Settings.Enabled"));
	form->addRow(enabled_);
	voiceTarget_ = spin(-30.0, -6.0, 0.5, " LUFS");
	voiceTarget_->setToolTip(qtr("AutoMixer.Settings.VoiceTarget.Tip"));
	form->addRow(qtr("AutoMixer.Settings.VoiceTarget"), voiceTarget_);
	mixTarget_ = spin(-30.0, -6.0, 0.5, " LUFS");
	form->addRow(qtr("AutoMixer.Settings.MixTarget"), mixTarget_);
	mixMax_ = spin(-24.0, 0.0, 0.5, " LUFS");
	mixMax_->setToolTip(qtr("AutoMixer.Settings.MixMax.Tip"));
	form->addRow(qtr("AutoMixer.Settings.MixMax"), mixMax_);
	streamTrack_ = new QComboBox;
	streamTrack_->addItem(qtr("AutoMixer.Settings.StreamTrack.Auto"), 0);
	for (int i = 1; i <= kTrackCount; i++)
		streamTrack_->addItem(QString::fromStdString(textf("AutoMixer.Dock.Track", i)), i);
	form->addRow(qtr("AutoMixer.Settings.StreamTrack"), streamTrack_);
	ceiling_ = spin(-6.0, 0.0, 0.1, " dBTP");
	ceiling_->setToolTip(qtr("AutoMixer.Settings.Ceiling.Tip"));
	form->addRow(qtr("AutoMixer.Settings.Ceiling"), ceiling_);
	auto *limits = new QHBoxLayout;
	minGain_ = spin(-60.0, 0.0, 1.0, " dB", 0);
	maxGain_ = spin(0.0, 24.0, 1.0, " dB", 0);
	limits->addWidget(minGain_);
	limits->addWidget(new QLabel(QStringLiteral("…")));
	limits->addWidget(maxGain_);
	limits->addStretch();
	form->addRow(qtr("AutoMixer.Settings.Limits"), limits);
	lockOnFader_ = new QCheckBox(qtr("AutoMixer.Settings.LockOnFader"));
	form->addRow(lockOnFader_);
	desktopHint_ = new QCheckBox(qtr("AutoMixer.Settings.DesktopHint"));
	form->addRow(desktopHint_);
	layout->addLayout(form);

	auto *filters = new QGroupBox(qtr("AutoMixer.Settings.Filters"));
	auto *filtersLayout = new QVBoxLayout(filters);
	filtersLayout->addWidget(note("AutoMixer.Settings.Filters.Text"));
	auto *buttons = new QHBoxLayout;
	auto *attach = new QPushButton(qtr("AutoMixer.Settings.Filters.Attach"));
	connect(attach, &QPushButton::clicked, this, [this] {
		const std::vector<std::string> missing = engine_->attachFilters(true);
		if (missing.empty()) {
			QMessageBox::information(this, qtr("AutoMixer"), qtr("AutoMixer.Settings.Filters.AllThere"));
			return;
		}
		QStringList names;
		for (const std::string &name : missing)
			names << QString::fromStdString(name);
		if (QMessageBox::question(this, qtr("AutoMixer"),
					  QString::fromUtf8(text("AutoMixer.Settings.Filters.AttachConfirm"))
						  .arg(names.join(QStringLiteral(", ")))) == QMessageBox::Yes)
			engine_->attachFilters();
	});
	auto *remove = new QPushButton(qtr("AutoMixer.Settings.Filters.Remove"));
	connect(remove, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, qtr("AutoMixer"), qtr("AutoMixer.Settings.Filters.RemoveConfirm")) !=
		    QMessageBox::Yes)
			return;
		const int removed = engine_->removeAllFilters();
		QMessageBox::information(this, qtr("AutoMixer"),
					 QString::fromStdString(textf("AutoMixer.History.FiltersRemoved", removed)));
		settings_ = engine_->settings();
	});
	buttons->addWidget(attach);
	buttons->addWidget(remove);
	buttons->addStretch();
	filtersLayout->addLayout(buttons);
	layout->addWidget(filters);
	layout->addStretch();
	return page;
}

QWidget *SettingsDialog::createRolesTab()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->addWidget(note("AutoMixer.Settings.Roles.Text"));
	roles_ = new QTableWidget(0, kColCount);
	roles_->setHorizontalHeaderLabels({qtr("AutoMixer.Settings.Roles.Source"), qtr("AutoMixer.Settings.Roles.Type"),
					   qtr("AutoMixer.Settings.Roles.Role"),
					   qtr("AutoMixer.Settings.Roles.Suggested"),
					   qtr("AutoMixer.Settings.Roles.Min"), qtr("AutoMixer.Settings.Roles.Max"),
					   qtr("AutoMixer.Settings.Roles.Locked")});
	roles_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
	roles_->verticalHeader()->hide();
	roles_->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(roles_, 1);

	auto *buttons = new QHBoxLayout;
	auto *suggest = new QPushButton(qtr("AutoMixer.Settings.Roles.UseSuggestions"));
	connect(suggest, &QPushButton::clicked, this, [this] {
		for (int row = 0; row < roles_->rowCount(); row++) {
			auto *combo = static_cast<QComboBox *>(roles_->cellWidget(row, kColRole));
			combo->setCurrentIndex(
				combo->findData(static_cast<int>(sources_[static_cast<size_t>(row)].suggested)));
		}
	});
	buttons->addWidget(suggest);
	buttons->addStretch();
	layout->addLayout(buttons);
	layout->addWidget(note("AutoMixer.Settings.Roles.Desktop"));
	return page;
}

QWidget *SettingsDialog::createPresetsTab()
{
	auto *page = new QWidget;
	auto *layout = new QHBoxLayout(page);

	auto *left = new QVBoxLayout;
	presetList_ = new QListWidget;
	connect(presetList_, &QListWidget::currentRowChanged, this, [this](int row) {
		storePreset();
		showPreset(row);
	});
	left->addWidget(presetList_, 1);
	auto *grid = new QGridLayout;
	auto *add = new QPushButton(qtr("AutoMixer.Presets.New"));
	add->setToolTip(qtr("AutoMixer.Presets.New.Tip"));
	connect(add, &QPushButton::clicked, this, [this] { addPreset(false); });
	auto *copy = new QPushButton(qtr("AutoMixer.Presets.Copy"));
	connect(copy, &QPushButton::clicked, this, [this] { addPreset(true); });
	presetDelete_ = new QPushButton(qtr("AutoMixer.Presets.Delete"));
	connect(presetDelete_, &QPushButton::clicked, this, &SettingsDialog::deletePreset);
	auto *import = new QPushButton(qtr("AutoMixer.Presets.Import"));
	connect(import, &QPushButton::clicked, this, &SettingsDialog::importPresets);
	presetExport_ = new QPushButton(qtr("AutoMixer.Presets.Export"));
	connect(presetExport_, &QPushButton::clicked, this, &SettingsDialog::exportPresets);
	grid->addWidget(add, 0, 0);
	grid->addWidget(copy, 0, 1);
	grid->addWidget(presetDelete_, 1, 0);
	grid->addWidget(import, 2, 0);
	grid->addWidget(presetExport_, 2, 1);
	left->addLayout(grid);
	layout->addLayout(left, 2);

	presetEditor_ = new QWidget;
	auto *form = new QFormLayout(presetEditor_);
	presetName_ = new QLineEdit;
	form->addRow(qtr("AutoMixer.Presets.Name"), presetName_);
	presetDescription_ = new QLineEdit;
	form->addRow(qtr("AutoMixer.Presets.Description"), presetDescription_);
	presetAnchor_ = roleCombo(false);
	presetAnchor_->setToolTip(qtr("AutoMixer.Presets.Anchor.Tip"));
	form->addRow(qtr("AutoMixer.Presets.Anchor"), presetAnchor_);
	presetTarget_ = spin(-40.0, -6.0, 0.5, " LUFS");
	form->addRow(qtr("AutoMixer.Presets.Target"), presetTarget_);

	auto *offsets = new QGridLayout;
	int column = 0;
	for (const dsp::Role role : kTargetRoles) {
		auto *box = spin(-40.0, 20.0, 0.5, " LU");
		presetOffsets_[static_cast<size_t>(role)] = box;
		offsets->addWidget(new QLabel(QString::fromUtf8(roleName(role))), (column / 2), (column % 2) * 2);
		offsets->addWidget(box, column / 2, (column % 2) * 2 + 1);
		column++;
	}
	form->addRow(qtr("AutoMixer.Presets.Offsets"), offsets);

	presetDuck_ = spin(0.0, 30.0, 0.5, " dB");
	form->addRow(qtr("AutoMixer.Presets.Ducking"), presetDuck_);
	auto *ducked = new QHBoxLayout;
	auto *triggers = new QHBoxLayout;
	for (const dsp::Role role : kTargetRoles) {
		auto *duckBox = new QCheckBox(QString::fromUtf8(roleName(role)));
		presetDucked_[static_cast<size_t>(role)] = duckBox;
		ducked->addWidget(duckBox);
		auto *triggerBox = new QCheckBox(QString::fromUtf8(roleName(role)));
		presetTriggers_[static_cast<size_t>(role)] = triggerBox;
		triggers->addWidget(triggerBox);
	}
	ducked->addStretch();
	triggers->addStretch();
	form->addRow(qtr("AutoMixer.Presets.Ducked"), ducked);
	form->addRow(qtr("AutoMixer.Presets.Triggers"), triggers);
	presetHold_ = new QSpinBox;
	presetHold_->setRange(0, 5000);
	presetHold_->setSingleStep(100);
	presetHold_->setSuffix(QStringLiteral(" ms"));
	form->addRow(qtr("AutoMixer.Presets.Hold"), presetHold_);
	presetSpeed_ = new QComboBox;
	presetSpeed_->addItem(qtr("AutoMixer.Speed.Slow"), static_cast<int>(dsp::Speed::Slow));
	presetSpeed_->addItem(qtr("AutoMixer.Speed.Normal"), static_cast<int>(dsp::Speed::Normal));
	presetSpeed_->addItem(qtr("AutoMixer.Speed.Fast"), static_cast<int>(dsp::Speed::Fast));
	form->addRow(qtr("AutoMixer.Presets.Speed"), presetSpeed_);
	presetGuard_ = new QCheckBox(qtr("AutoMixer.Presets.Guard"));
	presetGuard_->setToolTip(qtr("AutoMixer.Presets.Guard.Tip"));
	form->addRow(presetGuard_);

	auto *right = new QVBoxLayout;
	right->addWidget(presetEditor_);
	right->addWidget(note("AutoMixer.Presets.Builtin.Text"));
	right->addStretch();
	layout->addLayout(right, 5);
	return page;
}

QWidget *SettingsDialog::createScenesTab()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->addWidget(note("AutoMixer.Settings.Scenes.Text"));
	scenes_ = new QTableWidget(0, 2);
	scenes_->setHorizontalHeaderLabels(
		{qtr("AutoMixer.Settings.Scenes.Scene"), qtr("AutoMixer.Settings.Scenes.Preset")});
	scenes_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	scenes_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	scenes_->verticalHeader()->hide();
	scenes_->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(scenes_, 1);
	autoByActivity_ = new QCheckBox(qtr("AutoMixer.Settings.Scenes.Activity"));
	autoByActivity_->setToolTip(qtr("AutoMixer.Settings.Scenes.Activity.Tip"));
	layout->addWidget(autoByActivity_);
	return page;
}

QWidget *SettingsDialog::createWarningsTab()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	auto *form = new QFormLayout;
	warnLoud_ = spin(1.0, 20.0, 0.5, " LU");
	form->addRow(qtr("AutoMixer.Settings.WarnLoud"), warnLoud_);
	warnLoudSeconds_ = spin(0.5, 60.0, 0.5, " s");
	form->addRow(qtr("AutoMixer.Settings.WarnLoudSeconds"), warnLoudSeconds_);
	warnQuiet_ = spin(1.0, 30.0, 0.5, " LU");
	form->addRow(qtr("AutoMixer.Settings.WarnQuiet"), warnQuiet_);
	warnQuietSeconds_ = spin(0.5, 120.0, 0.5, " s");
	form->addRow(qtr("AutoMixer.Settings.WarnQuietSeconds"), warnQuietSeconds_);
	warnClip_ = spin(-6.0, 0.0, 0.1, " dBTP");
	form->addRow(qtr("AutoMixer.Settings.WarnClip"), warnClip_);
	muteWarning_ = new QCheckBox(qtr("AutoMixer.Settings.MuteWarning"));
	form->addRow(muteWarning_);
	layout->addLayout(form);
	layout->addWidget(note("AutoMixer.Settings.Warnings.Text"));
	layout->addStretch();
	return page;
}

void SettingsDialog::load()
{
	settings_ = engine_->settings();
	sources_ = engine_->knownSources();
	user_ = engine_->userPresets();
	builtin_.clear();
	for (const dsp::Preset &preset : engine_->presets()) {
		if (preset.builtin)
			builtin_.push_back(preset);
	}

	enabled_->setChecked(settings_.enabled);
	voiceTarget_->setValue(settings_.voiceTargetLufs);
	mixTarget_->setValue(settings_.mixTargetLufs);
	mixMax_->setValue(settings_.mixMaxLufs);
	streamTrack_->setCurrentIndex(streamTrack_->findData(settings_.streamTrack));
	ceiling_->setValue(settings_.ceilingDbtp);
	minGain_->setValue(settings_.defaultMinGainDb);
	maxGain_->setValue(settings_.defaultMaxGainDb);
	lockOnFader_->setChecked(settings_.lockOnFader);
	desktopHint_->setChecked(!settings_.desktopHintDismissed);

	roles_->setRowCount(static_cast<int>(sources_.size()));
	for (int row = 0; row < static_cast<int>(sources_.size()); row++) {
		const KnownSource &source = sources_[static_cast<size_t>(row)];
		auto *name = new QTableWidgetItem(QString::fromStdString(source.name));
		name->setFlags(Qt::ItemIsEnabled);
		roles_->setItem(row, kColName, name);
		auto *type = new QTableWidgetItem(QString::fromUtf8(obs_source_get_display_name(source.type.c_str())));
		type->setFlags(Qt::ItemIsEnabled);
		roles_->setItem(row, kColType, type);
		auto *role = roleCombo(true);
		role->setCurrentIndex(role->findData(static_cast<int>(source.role)));
		roles_->setCellWidget(row, kColRole, role);
		auto *suggested = new QTableWidgetItem(QString::fromUtf8(roleName(source.suggested)));
		suggested->setFlags(Qt::ItemIsEnabled);
		roles_->setItem(row, kColSuggested, suggested);
		auto *min = spin(-60.0, 0.0, 1.0, " dB", 0);
		min->setValue(source.minGainDb);
		roles_->setCellWidget(row, kColMin, min);
		auto *max = spin(0.0, 24.0, 1.0, " dB", 0);
		max->setValue(source.maxGainDb);
		roles_->setCellWidget(row, kColMax, max);
		auto *locked = new QCheckBox;
		locked->setChecked(source.locked);
		roles_->setCellWidget(row, kColLocked, locked);
	}
	roles_->resizeColumnsToContents();
	roles_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);

	// Szenen der aktuellen Sammlung mit ihrem Preset (oder „–“)
	scenes_->setRowCount(0);
	char **names = obs_frontend_get_scene_names();
	for (char **name = names; name && *name; name++) {
		const int row = scenes_->rowCount();
		scenes_->insertRow(row);
		auto *scene = new QTableWidgetItem(QString::fromUtf8(*name));
		scene->setFlags(Qt::ItemIsEnabled);
		scenes_->setItem(row, 0, scene);
		auto *preset = new QComboBox;
		preset->addItem(QStringLiteral("–"), QString());
		for (const dsp::Preset &p : builtin_)
			preset->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
		for (const dsp::Preset &p : user_)
			preset->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
		const ScenePreset *mapped = settings_.findScene(*name);
		preset->setCurrentIndex(
			std::max(0, preset->findData(mapped ? QString::fromStdString(mapped->preset) : QString())));
		scenes_->setCellWidget(row, 1, preset);
	}
	bfree(names);
	autoByActivity_->setChecked(settings_.autoByActivity);

	warnLoud_->setValue(settings_.warnings.loudLu);
	warnLoudSeconds_->setValue(settings_.warnings.loudSeconds);
	warnQuiet_->setValue(settings_.warnings.quietLu);
	warnQuietSeconds_->setValue(settings_.warnings.quietSeconds);
	warnClip_->setValue(settings_.warnings.clipDbtp);
	muteWarning_->setChecked(settings_.muteWarning);

	int select = 0;
	const std::string active = settings_.preset;
	for (size_t i = 0; i < builtin_.size() + user_.size(); i++) {
		const dsp::Preset &preset = i < builtin_.size() ? builtin_[i] : user_[i - builtin_.size()];
		if (preset.id == active)
			select = static_cast<int>(i);
	}
	refreshPresetList(select);
}

void SettingsDialog::apply()
{
	storePreset();
	// Aktuellen Stand holen, damit inzwischen geänderte Werte (z. B. Sperren im Dock) nicht verloren gehen
	Settings s = engine_->settings();
	s.enabled = enabled_->isChecked();
	s.voiceTargetLufs = static_cast<float>(voiceTarget_->value());
	s.mixTargetLufs = static_cast<float>(mixTarget_->value());
	s.mixMaxLufs = static_cast<float>(mixMax_->value());
	s.streamTrack = streamTrack_->currentData().toInt();
	s.ceilingDbtp = static_cast<float>(ceiling_->value());
	s.defaultMinGainDb = static_cast<float>(minGain_->value());
	s.defaultMaxGainDb = static_cast<float>(maxGain_->value());
	s.lockOnFader = lockOnFader_->isChecked();
	s.desktopHintDismissed = !desktopHint_->isChecked();

	for (int row = 0; row < roles_->rowCount(); row++) {
		const KnownSource &known = sources_[static_cast<size_t>(row)];
		SourceSettings *source = s.findSource(known.uuid, known.name);
		if (!source)
			continue;
		const auto role = static_cast<dsp::Role>(
			static_cast<QComboBox *>(roles_->cellWidget(row, kColRole))->currentData().toInt());
		if (role != source->role) {
			source->role = role;
			source->roleChosen = true;
		}
		source->minGainDb =
			static_cast<float>(static_cast<QDoubleSpinBox *>(roles_->cellWidget(row, kColMin))->value());
		source->maxGainDb =
			static_cast<float>(static_cast<QDoubleSpinBox *>(roles_->cellWidget(row, kColMax))->value());
		source->locked = static_cast<QCheckBox *>(roles_->cellWidget(row, kColLocked))->isChecked();
	}

	// Zuordnungen der Szenen, die es gerade gibt, neu setzen; die anderer Sammlungen bleiben
	for (int row = 0; row < scenes_->rowCount(); row++) {
		const std::string scene = scenes_->item(row, 0)->text().toStdString();
		const std::string preset =
			static_cast<QComboBox *>(scenes_->cellWidget(row, 1))->currentData().toString().toStdString();
		s.scenePresets.erase(std::remove_if(s.scenePresets.begin(), s.scenePresets.end(),
						    [&](const ScenePreset &entry) { return entry.scene == scene; }),
				     s.scenePresets.end());
		if (!preset.empty())
			s.scenePresets.push_back({scene, preset});
	}
	s.autoByActivity = autoByActivity_->isChecked();

	s.warnings.loudLu = static_cast<float>(warnLoud_->value());
	s.warnings.loudSeconds = static_cast<float>(warnLoudSeconds_->value());
	s.warnings.quietLu = static_cast<float>(warnQuiet_->value());
	s.warnings.quietSeconds = static_cast<float>(warnQuietSeconds_->value());
	s.warnings.clipDbtp = static_cast<float>(warnClip_->value());
	s.muteWarning = muteWarning_->isChecked();

	engine_->setUserPresets(user_);
	// Gelöschtes Preset ausgewählt? Dann zurück auf das Standard-Preset
	const std::vector<dsp::Preset> all = engine_->presets();
	if (s.preset != "auto" &&
	    std::none_of(all.begin(), all.end(), [&](const dsp::Preset &p) { return p.id == s.preset; }))
		s.preset = builtin_.empty() ? std::string() : builtin_.front().id;
	engine_->setSettings(s);
	settings_ = s;
	updateHotkeyDescriptions();
}

void SettingsDialog::refreshPresetList(int select)
{
	const QSignalBlocker blocker(presetList_);
	presetList_->clear();
	for (const dsp::Preset &preset : builtin_)
		presetList_->addItem(QString::fromStdString(preset.name) + QStringLiteral("  ") +
				     qtr("AutoMixer.Presets.BuiltinMark"));
	for (const dsp::Preset &preset : user_)
		presetList_->addItem(QString::fromStdString(preset.name));
	presetIndex_ = -1;
	if (presetList_->count() == 0)
		return;
	presetList_->setCurrentRow(std::clamp(select, 0, presetList_->count() - 1));
	showPreset(presetList_->currentRow());
}

dsp::Preset *SettingsDialog::editablePreset()
{
	const int userIndex = presetIndex_ - static_cast<int>(builtin_.size());
	if (userIndex < 0 || userIndex >= static_cast<int>(user_.size()))
		return nullptr;
	return &user_[static_cast<size_t>(userIndex)];
}

void SettingsDialog::showPreset(int index)
{
	presetIndex_ = index;
	const int total = static_cast<int>(builtin_.size() + user_.size());
	if (index < 0 || index >= total)
		return;
	const bool builtin = index < static_cast<int>(builtin_.size());
	const dsp::Preset &preset = builtin ? builtin_[static_cast<size_t>(index)]
					    : user_[static_cast<size_t>(index) - builtin_.size()];
	loadingPreset_ = true;
	presetName_->setText(QString::fromStdString(preset.name));
	presetDescription_->setText(QString::fromStdString(preset.description));
	presetAnchor_->setCurrentIndex(presetAnchor_->findData(static_cast<int>(preset.anchor)));
	presetTarget_->setValue(preset.targetLufs);
	for (const dsp::Role role : kTargetRoles) {
		const size_t i = static_cast<size_t>(role);
		presetOffsets_[i]->setValue(preset.offsets[i]);
		presetDucked_[i]->setChecked(preset.ducked[i]);
		presetTriggers_[i]->setChecked(preset.duckTriggers[i]);
	}
	presetDuck_->setValue(preset.duckDb);
	presetHold_->setValue(static_cast<int>(preset.duckHoldMs));
	presetSpeed_->setCurrentIndex(presetSpeed_->findData(static_cast<int>(preset.speed)));
	presetGuard_->setChecked(preset.screamGuard);
	loadingPreset_ = false;

	// Mitgelieferte Presets sind schreibgeschützt; „Kopieren“ macht ein eigenes daraus
	presetEditor_->setEnabled(!builtin);
	presetDelete_->setEnabled(!builtin);
	presetExport_->setEnabled(true);
}

void SettingsDialog::storePreset()
{
	dsp::Preset *preset = editablePreset();
	if (!preset || loadingPreset_)
		return;
	preset->name = presetName_->text().trimmed().toStdString();
	if (preset->name.empty())
		preset->name = preset->id;
	preset->description = presetDescription_->text().trimmed().toStdString();
	preset->anchor = static_cast<dsp::Role>(presetAnchor_->currentData().toInt());
	preset->targetLufs = static_cast<float>(presetTarget_->value());
	for (const dsp::Role role : kTargetRoles) {
		const size_t i = static_cast<size_t>(role);
		preset->offsets[i] = static_cast<float>(presetOffsets_[i]->value());
		preset->ducked[i] = presetDucked_[i]->isChecked();
		preset->duckTriggers[i] = presetTriggers_[i]->isChecked();
	}
	preset->offsets[static_cast<size_t>(preset->anchor)] = 0.0f;
	preset->duckDb = static_cast<float>(presetDuck_->value());
	preset->duckHoldMs = static_cast<float>(presetHold_->value());
	preset->speed = static_cast<dsp::Speed>(presetSpeed_->currentData().toInt());
	preset->screamGuard = presetGuard_->isChecked();
	QListWidgetItem *item = presetList_->item(presetIndex_);
	if (item)
		item->setText(QString::fromStdString(preset->name));
}

void SettingsDialog::addPreset(bool copyCurrent)
{
	storePreset();
	dsp::Preset preset;
	const int total = static_cast<int>(builtin_.size() + user_.size());
	if (copyCurrent && presetIndex_ >= 0 && presetIndex_ < total) {
		preset = presetIndex_ < static_cast<int>(builtin_.size())
				 ? builtin_[static_cast<size_t>(presetIndex_)]
				 : user_[static_cast<size_t>(presetIndex_) - builtin_.size()];
		preset.name = textf("AutoMixer.Presets.CopyName", preset.name.c_str());
	} else {
		// „Aktuelle Einstellung als Preset speichern“: vom gerade wirksamen Preset ausgehen
		preset = engine_->activePreset();
		preset.name = text("AutoMixer.Presets.NewName");
	}
	std::vector<dsp::Preset> all = builtin_;
	all.insert(all.end(), user_.begin(), user_.end());
	preset.id = newUserPresetId(all);
	preset.builtin = false;
	user_.push_back(preset);
	refreshPresetList(static_cast<int>(builtin_.size() + user_.size()) - 1);
}

void SettingsDialog::deletePreset()
{
	const int userIndex = presetIndex_ - static_cast<int>(builtin_.size());
	if (userIndex < 0 || userIndex >= static_cast<int>(user_.size()))
		return;
	user_.erase(user_.begin() + userIndex);
	presetIndex_ = -1;
	refreshPresetList(static_cast<int>(builtin_.size()) + userIndex - 1);
}

void SettingsDialog::importPresets()
{
	storePreset();
	const QString path = QFileDialog::getOpenFileName(this, qtr("AutoMixer.Presets.Import"), QString(),
							  QStringLiteral("JSON (*.json)"));
	if (path.isEmpty())
		return;
	std::vector<dsp::Preset> imported;
	std::string error;
	if (!am::importPresets(path.toStdString(), imported, error)) {
		QMessageBox::warning(this, qtr("AutoMixer"), QString::fromStdString(error));
		return;
	}
	for (dsp::Preset &preset : imported) {
		std::vector<dsp::Preset> all = builtin_;
		all.insert(all.end(), user_.begin(), user_.end());
		preset.id = newUserPresetId(all); // nie ein vorhandenes Preset überschreiben
		preset.builtin = false;
		user_.push_back(preset);
	}
	refreshPresetList(static_cast<int>(builtin_.size() + user_.size()) - 1);
}

void SettingsDialog::exportPresets()
{
	storePreset();
	const int total = static_cast<int>(builtin_.size() + user_.size());
	if (presetIndex_ < 0 || presetIndex_ >= total)
		return;
	const dsp::Preset &preset = presetIndex_ < static_cast<int>(builtin_.size())
					    ? builtin_[static_cast<size_t>(presetIndex_)]
					    : user_[static_cast<size_t>(presetIndex_) - builtin_.size()];
	const QString path = QFileDialog::getSaveFileName(this, qtr("AutoMixer.Presets.Export"),
							  QString::fromStdString(preset.id) + QStringLiteral(".json"),
							  QStringLiteral("JSON (*.json)"));
	if (path.isEmpty())
		return;
	if (!am::exportPresets(path.toStdString(), {preset}))
		QMessageBox::warning(this, qtr("AutoMixer"), qtr("AutoMixer.Presets.ExportFailed"));
}

} // namespace am
