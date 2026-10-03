# Plugins

Drop a folder here to add native intrinsics to the engine.  Everything under
`plugins/` is compiled into the one engine binary; adding a plugin needs no
change to `CMakeLists.txt` or `src/main.cpp`.  Re-run CMake (`cmake -S . -B build`)
after adding or removing a folder.

```
plugins/mything/
  plugin.cpp     required: defines the hooks below
  plugin.cmake   optional: include dirs, defines, libraries
  *.cpp *.h      any other sources are picked up automatically
```

The folder name must be a valid C identifier; it is the name used in the hooks.

```cpp
#include "Plugin.h"

static void Fill(MiniScript::ValueDict& m) { /* m.SetValue(String("fn"), f.GetFunc()); */ }

MS_PLUGIN_INIT(mything)     { PluginAddModule<&Fill>("mything"); }  // required
MS_PLUGIN_UPDATE(mything)   { }   // optional: every frame, main thread
MS_PLUGIN_RESET(mything)    { }   // optional: before the script (re)starts
MS_PLUGIN_SHUTDOWN(mything) { }   // optional: app closing (reverse load order)
```

See `quadtree/plugin.cpp` for a complete example.

## plugin.cmake

Runs in the top-level scope, with `PLUGIN_NAME` and `PLUGIN_DIR` set, after the
platform sections of `CMakeLists.txt`:

```cmake
if(EMSCRIPTEN)
    set(MS_PLUGIN_SKIP TRUE)                  # leave this plugin out on web
else()
    target_compile_definitions(raylib-miniscript PRIVATE MYTHING_ENABLED)
    target_include_directories(raylib-miniscript PRIVATE ${PLUGIN_DIR}/sdk/include)
    target_link_libraries(raylib-miniscript ${PLUGIN_DIR}/sdk/libmything.a)  # no PRIVATE/PUBLIC
endif()
```

Leave plugins out of a build with `-DMS_DISABLE_PLUGINS="mything;other"`.

## Guidance

- Register under your own module name (`PluginAddModule`), so you can't clobber core intrinsics.
- Batch work: let a script make one native call for many items, not one per item.
- Keep native objects in GC handles with a finalizer (see `NewNativeHandle` in `src/RaylibTypes.h`).
- Async SDK results (purchases, ads) should go into a queue the script polls; never call into the VM from another thread. Pump the SDK in `MS_PLUGIN_UPDATE`.
- Guard platform-specific code with `PLATFORM_DESKTOP` / `PLATFORM_WEB`.
- Plugins are trusted code: they run in-process with full access, outside the file-system sandbox.
