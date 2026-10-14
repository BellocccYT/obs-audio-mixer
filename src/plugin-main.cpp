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
#include "engine.hpp"
#include "filter.hpp"
#include "hotkeys.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

am::Engine *g_engine = nullptr;
QPointer<am::MixerDock> g_dock;
bool g_callbackRegistered = false;

// OBS blendet neue Plugin-Docks standardmäßig aus (und lässt sie schweben). Beim allerersten Start
// docken wir es rechts an und zeigen es einmal, danach merkt sich OBS selbst, wo und ob es offen ist.
void introduceDock()
{
	am::Settings settings = g_engine->settings();
	if (settings.dockIntroduced || !g_dock)
		return;
	auto *dockWidget = qobject_cast<QDockWidget *>(g_dock->parentWidget());
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (dockWidget && mainWindow) {
		dockWidget->setFloating(false);
		mainWindow->addDockWidget(Qt::RightDockWidgetArea, dockWidget);
		dockWidget->show();
		mainWindow->resizeDocks({dockWidget}, {380}, Qt::Horizontal);
	}
	settings.dockIntroduced = true;
	g_engine->setSettings(settings);
}

// Name der Szene im Programm (für die Zuordnung Szene → Preset)
void updateCurrentScene()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	g_engine->setCurrentScene(scene ? obs_source_get_name(scene) : "");
	obs_source_release(scene);
}

// Auf welchen Spuren gerade gestreamt wird
void updateStreamMixers(bool live)
{
	uint32_t mixers = 0;
	if (live) {
		obs_output_t *output = obs_frontend_get_streaming_output();
		if (output) {
			mixers = static_cast<uint32_t>(obs_output_get_mixers(output));
			obs_output_release(output);
		}
	}
	g_engine->setStreamMixers(mixers);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		g_engine->start();
		updateStreamMixers(obs_frontend_streaming_active());
		updateCurrentScene();
		am::updateHotkeyDescriptions();
		introduceDock();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		updateCurrentScene();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		// Gleich werden alle Quellen zerstört: vorher loslassen
		g_engine->suspend();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		g_engine->resume();
		updateCurrentScene();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		updateStreamMixers(true);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		updateStreamMixers(false);
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		// Kommt beim Beenden direkt vor dem Abräumen aller Quellen: Callbacks abmelden, solange es sie gibt
		if (g_dock)
			g_dock->shutdown();
		g_engine->suspend();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		if (g_dock)
			g_dock->shutdown();
		g_engine->stop();
		am::stopHotkeySaving(); // OBS hat schon zum letzten Mal gespeichert
		// Beim Entladen gibt es die Frontend-API nicht mehr; das Abmelden während der Verteilung ist erlaubt
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
		g_callbackRegistered = false;
		break;
	default:
		break;
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Auto Mixer";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Misst die Lautheit aller Audioquellen und gleicht zu laute oder zu leise Tonspuren automatisch aus.";
}

bool obs_module_load(void)
{
	am::registerFilter();
	g_engine = new am::Engine();

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (mainWindow) {
		auto *dock = new am::MixerDock(g_engine, mainWindow);
		if (obs_frontend_add_dock_by_id("auto-mixer", obs_module_text("AutoMixer"), dock))
			g_dock = dock;
		else
			delete dock;
	}
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	g_callbackRegistered = true;
	am::registerHotkeys(g_engine);

	obs_log(LOG_INFO, "geladen (Version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (g_callbackRegistered)
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	am::unregisterHotkeys();
	if (g_engine) {
		g_engine->stop();
		delete g_engine;
		g_engine = nullptr;
	}
	obs_log(LOG_INFO, "entladen");
}
