//
//  Plugin.h
//  raylib-miniscript
//
//  Compile-time plugins.  A folder under plugins/ is compiled into the engine
//  and its hooks are called at the right moments; see plugins/README.md.
//  CMake discovers the folders and generates the table of hooks, so adding a
//  plugin touches neither the build files nor main.cpp.
//

#ifndef PLUGIN_H
#define PLUGIN_H

#include "miniscript.h"

// What a plugin provides, in one descriptor.  A plugin's plugin.cpp contains
// exactly one MS_PLUGIN(...) line, naming its hooks; the preprocessor (not the
// build system) decides which are null, so hooks may sit inside #ifdef blocks:
//
//     static bool Init() { ...; return true; }   // false: unavailable on this run
//     MS_PLUGIN(steam, "1.0", Init,
//     #ifdef PLATFORM_DESKTOP
//         Update,
//     #else
//         nullptr,
//     #endif
//         nullptr, Shutdown)
//
// The folder name <name> must be a valid C identifier.
struct MSPluginHooks {
	const char* version;     // reported to scripts by `plugins`; "" if none
	bool (*init)();          // required: add intrinsics, once, at interpreter creation.
	                         //   Return false if the plugin cannot work this run (Steam
	                         //   not running...); it is then marked unavailable, its other
	                         //   hooks never run, and it must undo its own partial setup.
	void (*update)();        // optional: once per frame, on the main thread
	void (*reset)();         // optional: the interpreter is about to be reset
	void (*shutdown)();      // optional: the app is closing
};

#define MS_PLUGIN(name, version, init, update, reset, shutdown) \
	const MSPluginHooks* MSPlugin_##name() { \
		static const MSPluginHooks hooks = { version, init, update, reset, shutdown }; \
		return &hooks; \
	}

// One row of the generated table (build/plugin_registry.cpp).  The table ends
// with a row whose name is null.
struct MSPluginEntry {
	const char* name;
	const MSPluginHooks* (*get)();
};
extern const MSPluginEntry kMSPlugins[];

// Called from main.cpp.
void PluginsInit();
void PluginsUpdate();
void PluginsReset();
void PluginsShutdown();

// Make a global module (like `physicsCore`) named `name`, whose members `Fill`
// adds to the map it is given.  Call from your init hook:
//
//     static void Fill(MiniScript::ValueDict& m) { ... m.SetValue(String("new"), f.GetFunc()); }
//     static bool Init() { PluginAddModule<&Fill>("quadtree"); return true; }
//
// Registering under its own name keeps a plugin from clobbering core intrinsics.
template<void (*Fill)(MiniScript::ValueDict&)>
void PluginAddModule(const char* name) {
	using namespace MiniScript;
	Intrinsic f = Intrinsic::Create(String(name));
	f.set_Code([](Context context, IntrinsicResult partialResult) -> IntrinsicResult {
		// Built on first use, then wrapped and GC-rooted (see PhysicsCore.cpp).
		static ValueDict module;
		static Value moduleValue;
		if (moduleValue.IsNull()) {
			Fill(module);
			moduleValue = GCManager::NewMapFromDict(module);
			GCManager::AddRoot(moduleValue);
		}
		return IntrinsicResult(moduleValue);
	});
}

#endif // PLUGIN_H
