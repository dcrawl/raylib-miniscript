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

// Hooks a plugin implements.  Folder name <name> must be a valid C identifier.
//   MS_PLUGIN_INIT      required: add intrinsics (once, at interpreter creation)
//   MS_PLUGIN_UPDATE    optional: once per frame, on the main thread
//   MS_PLUGIN_RESET     optional: the interpreter is about to be reset
//   MS_PLUGIN_SHUTDOWN  optional: the app is closing
#define MS_PLUGIN_INIT(name)     void MSPlugin_##name##_Init()
#define MS_PLUGIN_UPDATE(name)   void MSPlugin_##name##_Update()
#define MS_PLUGIN_RESET(name)    void MSPlugin_##name##_Reset()
#define MS_PLUGIN_SHUTDOWN(name) void MSPlugin_##name##_Shutdown()

// One row of the generated table (build/plugin_registry.cpp).  A hook the
// plugin does not define is null.  The table ends with a row whose name is null.
struct MSPluginEntry {
	const char* name;
	void (*init)();
	void (*update)();
	void (*reset)();
	void (*shutdown)();
};
extern const MSPluginEntry kMSPlugins[];

// Called from main.cpp.
void PluginsInit();
void PluginsUpdate();
void PluginsReset();
void PluginsShutdown();

// Make a global module (like `physicsCore`) named `name`, whose members `Fill`
// adds to the map it is given.  Call from MS_PLUGIN_INIT:
//
//     static void Fill(MiniScript::ValueDict& m) { ... m.SetValue(String("new"), f.GetFunc()); }
//     MS_PLUGIN_INIT(quadtree) { PluginAddModule<&Fill>("quadtree"); }
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
