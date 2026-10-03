//
//  Plugin.cpp
//  raylib-miniscript
//
//  Walks the generated plugin table (kMSPlugins) at the engine's lifecycle points.
//

#include "Plugin.h"
#include "raylib.h"

void PluginsInit() {
	for (const MSPluginEntry* p = kMSPlugins; p->name != nullptr; p++) {
		TraceLog(LOG_INFO, "PLUGIN: Loading %s", p->name);
		if (p->init) p->init();
	}
}

void PluginsUpdate() {
	for (const MSPluginEntry* p = kMSPlugins; p->name != nullptr; p++) {
		if (p->update) p->update();
	}
}

void PluginsReset() {
	for (const MSPluginEntry* p = kMSPlugins; p->name != nullptr; p++) {
		if (p->reset) p->reset();
	}
}

void PluginsShutdown() {
	// Reverse order, so a plugin can rely on those loaded before it.
	const MSPluginEntry* end = kMSPlugins;
	while (end->name != nullptr) end++;
	while (end != kMSPlugins) {
		--end;
		if (end->shutdown) end->shutdown();
	}
}
