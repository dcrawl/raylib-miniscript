#include "RaylibIntrinsics.h"
#include "RaylibTypes.h"
#include "macros.h"

#include "MiniscriptInterpreter.h"
#include "MiniscriptTAC.h"
#include "MiniscriptTypes.h"

#ifdef Assert
#undef Assert
#endif

#ifdef HAVE_RMLUI
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/SystemInterface.h>
#endif

#include "raylib.h"
#include "rlgl.h"

#include <map>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniScript;

#ifdef HAVE_RMLUI
namespace {

struct RmlUiGeometry {
	std::vector<Rml::Vertex> vertices;
	std::vector<int> indices;
	Rml::TextureHandle texture = 0;
};

class RaylibRmlSystemInterface : public Rml::SystemInterface {
public:
	double GetElapsedTime() override {
		return GetTime();
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
		int raylog = LOG_INFO;
		switch (type) {
			case Rml::Log::LT_ALWAYS: raylog = LOG_INFO; break;
			case Rml::Log::LT_ERROR:
			case Rml::Log::LT_ASSERT: raylog = LOG_ERROR; break;
			case Rml::Log::LT_WARNING: raylog = LOG_WARNING; break;
			case Rml::Log::LT_INFO: raylog = LOG_INFO; break;
			case Rml::Log::LT_DEBUG: raylog = LOG_DEBUG; break;
			default: raylog = LOG_INFO; break;
		}
		TraceLog(raylog, "RmlUi: %s", message.c_str());
		return true;
	}
};

class RaylibRmlRenderInterface : public Rml::RenderInterfaceCompatibility {
public:
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture) override {
		if (vertices == nullptr || indices == nullptr || num_vertices <= 0 || num_indices <= 0) return 0;
		auto geo = std::make_unique<RmlUiGeometry>();
		geo->vertices.assign(vertices, vertices + num_vertices);
		geo->indices.assign(indices, indices + num_indices);
		geo->texture = texture;
		Rml::CompiledGeometryHandle handle = nextGeometryHandle++;
		geometry[handle] = std::move(geo);
		return handle;
	}

	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry_handle, const Rml::Vector2f& translation) override {
		auto it = geometry.find(geometry_handle);
		if (it == geometry.end()) return;
		RenderCompiledGeometry(geometry_handle, translation, it->second->texture);
	}

	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry_handle, const Rml::Vector2f& translation, Rml::TextureHandle texture) {
		auto it = geometry.find(geometry_handle);
		if (it == geometry.end()) return;

		const RmlUiGeometry& geo = *it->second;
		if (geo.indices.empty()) return;

		Texture2D tex = {};
		bool hasTexture = false;
		auto texIt = textures.find(texture);
		if (texIt != textures.end()) {
			hasTexture = true;
			tex = texIt->second;
		}

		rlSetTexture(hasTexture ? tex.id : 0);
		rlBegin(RL_TRIANGLES);
		for (size_t i = 0; i < geo.indices.size(); i++) {
			int vi = geo.indices[i];
			if (vi < 0 || vi >= (int)geo.vertices.size()) continue;

			const Rml::Vertex& v = geo.vertices[vi];
			rlColor4ub((unsigned char)v.colour.red, (unsigned char)v.colour.green, (unsigned char)v.colour.blue, (unsigned char)v.colour.alpha);
			rlTexCoord2f(v.tex_coord.x, v.tex_coord.y);
			rlVertex2f(v.position.x + translation.x, v.position.y + translation.y);
		}
		rlEnd();
		if (hasTexture) rlSetTexture(0);
	}

	void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry_handle) override {
		geometry.erase(geometry_handle);
	}

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture, const Rml::Vector2f& translation) override {
		Rml::CompiledGeometryHandle handle = CompileGeometry(vertices, num_vertices, indices, num_indices, texture);
		if (handle == 0) return;
		RenderCompiledGeometry(handle, translation, texture);
		ReleaseCompiledGeometry(handle);
	}

	void EnableScissorRegion(bool enable) override {
		scissorEnabled = enable;
		if (!enable) EndScissorMode();
		else BeginScissorMode(scissorRegion.x, scissorRegion.y, scissorRegion.width, scissorRegion.height);
	}

	void SetScissorRegion(int x, int y, int width, int height) override {
		scissorRegion = {(float)x, (float)y, (float)width, (float)height};
		if (scissorEnabled) BeginScissorMode(x, y, width, height);
	}

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
		Image img = LoadImage(source.c_str());
		if (!img.data) {
			texture_handle = 0;
			texture_dimensions = {0, 0};
			return false;
		}
		Texture2D tex = LoadTextureFromImage(img);
		UnloadImage(img);
		if (tex.id == 0) {
			texture_handle = 0;
			texture_dimensions = {0, 0};
			return false;
		}

		texture_handle = nextTextureHandle++;
		textures[texture_handle] = tex;
		texture_dimensions = {tex.width, tex.height};
		return true;
	}

	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override {
		if (source == nullptr || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
			texture_handle = 0;
			return false;
		}

		Image img = {
			(void*)source,
			source_dimensions.x,
			source_dimensions.y,
			1,
			PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
		};
		Texture2D tex = LoadTextureFromImage(img);
		if (tex.id == 0) {
			texture_handle = 0;
			return false;
		}

		texture_handle = nextTextureHandle++;
		textures[texture_handle] = tex;
		return true;
	}

	void ReleaseTexture(Rml::TextureHandle texture_handle) override {
		auto it = textures.find(texture_handle);
		if (it == textures.end()) return;
		UnloadTexture(it->second);
		textures.erase(it);
	}

	void ReleaseAllTextures() {
		for (auto& pair : textures) {
			UnloadTexture(pair.second);
		}
		textures.clear();
	}

private:
	Rml::CompiledGeometryHandle nextGeometryHandle = 1;
	Rml::TextureHandle nextTextureHandle = 1;
	std::map<Rml::CompiledGeometryHandle, std::unique_ptr<RmlUiGeometry>> geometry;
	std::map<Rml::TextureHandle, Texture2D> textures;
	bool scissorEnabled = false;
	Rectangle scissorRegion = {0, 0, 0, 0};
};

struct RmlUiEventBridge;

struct ElementListenerBinding {
	RmlUiEventBridge* listener = nullptr;
	std::string eventType;
};

struct RmlUiState {
	bool initialized = false;
	std::unique_ptr<RaylibRmlSystemInterface> systemInterface;
	std::unique_ptr<RaylibRmlRenderInterface> renderInterface;
	std::map<Rml::Context*, std::string> contextNames;
	std::map<Rml::Element*, std::vector<ElementListenerBinding>> listenersByElement;
};

RmlUiState g_rmlui;

ValueDict RmlUiContextClass() {
	static ValueDict map;
	if (map.Count() == 0) {
		map.SetValue(String("_handle"), Value::zero);
		map.SetValue(String("_name"), Value::null);
		map.SetValue(String("width"), Value::zero);
		map.SetValue(String("height"), Value::zero);
	}
	return map;
}

ValueDict RmlUiDocumentClass() {
	static ValueDict map;
	if (map.Count() == 0) {
		map.SetValue(String("_handle"), Value::zero);
		map.SetValue(String("_contextHandle"), Value::zero);
		map.SetValue(String("id"), Value::null);
		map.SetValue(String("title"), Value::null);
	}
	return map;
}

ValueDict RmlUiElementClass() {
	static ValueDict map;
	if (map.Count() == 0) {
		map.SetValue(String("_handle"), Value::zero);
		map.SetValue(String("_contextHandle"), Value::zero);
		map.SetValue(String("tag"), Value::null);
		map.SetValue(String("id"), Value::null);
		map.SetValue(String("classes"), Value::null);
	}
	return map;
}

Value RmlUiContextToValue(Rml::Context* ctx) {
	if (ctx == nullptr) return Value::null;
	ValueDict map;
	map.SetValue(Value::magicIsA, RmlUiContextClass());
	map.SetValue(String("_handle"), PointerToValue(ctx));
	map.SetValue(String("_name"), Value(String(ctx->GetName().c_str())));
	Rml::Vector2i dims = ctx->GetDimensions();
	map.SetValue(String("width"), Value(dims.x));
	map.SetValue(String("height"), Value(dims.y));
	return Value(map);
}

Rml::Context* ValueToRmlUiContext(Value value) {
	if (value.type != ValueType::Map) return nullptr;
	return (Rml::Context*)ValueToPointer(value.GetDict().Lookup(String("_handle"), Value::zero));
}

Value RmlUiDocumentToValue(Rml::ElementDocument* doc) {
	if (doc == nullptr) return Value::null;
	ValueDict map;
	map.SetValue(Value::magicIsA, RmlUiDocumentClass());
	map.SetValue(String("_handle"), PointerToValue(doc));
	map.SetValue(String("_contextHandle"), PointerToValue(doc->GetContext()));
	map.SetValue(String("id"), Value(String(doc->GetId().c_str())));
	map.SetValue(String("title"), Value(String(doc->GetTitle().c_str())));
	return Value(map);
}

Rml::ElementDocument* ValueToRmlUiDocument(Value value) {
	if (value.type != ValueType::Map) return nullptr;
	return (Rml::ElementDocument*)ValueToPointer(value.GetDict().Lookup(String("_handle"), Value::zero));
}

Value RmlUiElementToValue(Rml::Element* el) {
	if (el == nullptr) return Value::null;
	ValueDict map;
	map.SetValue(Value::magicIsA, RmlUiElementClass());
	map.SetValue(String("_handle"), PointerToValue(el));
	map.SetValue(String("_contextHandle"), PointerToValue(el->GetContext()));
	map.SetValue(String("tag"), Value(String(el->GetTagName().c_str())));
	map.SetValue(String("id"), Value(String(el->GetId().c_str())));
	map.SetValue(String("classes"), Value(String(el->GetClassNames().c_str())));
	return Value(map);
}

Rml::Element* ValueToRmlUiElement(Value value) {
	if (value.type != ValueType::Map) return nullptr;
	return (Rml::Element*)ValueToPointer(value.GetDict().Lookup(String("_handle"), Value::zero));
}

bool EnsureRmlUiInitialized() {
	if (g_rmlui.initialized) return true;
	g_rmlui.systemInterface = std::make_unique<RaylibRmlSystemInterface>();
	g_rmlui.renderInterface = std::make_unique<RaylibRmlRenderInterface>();
	Rml::SetSystemInterface(g_rmlui.systemInterface.get());
	Rml::SetRenderInterface(g_rmlui.renderInterface->GetAdaptedInterface());
	if (!Rml::Initialise()) {
		g_rmlui.systemInterface.reset();
		g_rmlui.renderInterface.reset();
		return false;
	}
	g_rmlui.initialized = true;
	return true;
}

int CurrentKeyModifiers() {
	int mods = 0;
	if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= (int)Rml::Input::KM_CTRL;
	if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods |= (int)Rml::Input::KM_SHIFT;
	if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods |= (int)Rml::Input::KM_ALT;
	if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) mods |= (int)Rml::Input::KM_META;
	return mods;
}

Rml::Input::KeyIdentifier RaylibKeyToRmlKey(int key) {
	if (key >= KEY_A && key <= KEY_Z) {
		return (Rml::Input::KeyIdentifier)((int)Rml::Input::KI_A + (key - KEY_A));
	}
	if (key >= KEY_ZERO && key <= KEY_NINE) {
		return (Rml::Input::KeyIdentifier)((int)Rml::Input::KI_0 + (key - KEY_ZERO));
	}
	switch (key) {
		case KEY_SPACE: return Rml::Input::KI_SPACE;
		case KEY_ENTER: return Rml::Input::KI_RETURN;
		case KEY_TAB: return Rml::Input::KI_TAB;
		case KEY_BACKSPACE: return Rml::Input::KI_BACK;
		case KEY_ESCAPE: return Rml::Input::KI_ESCAPE;
		case KEY_INSERT: return Rml::Input::KI_INSERT;
		case KEY_DELETE: return Rml::Input::KI_DELETE;
		case KEY_RIGHT: return Rml::Input::KI_RIGHT;
		case KEY_LEFT: return Rml::Input::KI_LEFT;
		case KEY_DOWN: return Rml::Input::KI_DOWN;
		case KEY_UP: return Rml::Input::KI_UP;
		case KEY_PAGE_UP: return Rml::Input::KI_PRIOR;
		case KEY_PAGE_DOWN: return Rml::Input::KI_NEXT;
		case KEY_HOME: return Rml::Input::KI_HOME;
		case KEY_END: return Rml::Input::KI_END;
		case KEY_CAPS_LOCK: return Rml::Input::KI_CAPITAL;
		case KEY_SCROLL_LOCK: return Rml::Input::KI_SCROLL;
		case KEY_NUM_LOCK: return Rml::Input::KI_NUMLOCK;
		case KEY_F1: return Rml::Input::KI_F1;
		case KEY_F2: return Rml::Input::KI_F2;
		case KEY_F3: return Rml::Input::KI_F3;
		case KEY_F4: return Rml::Input::KI_F4;
		case KEY_F5: return Rml::Input::KI_F5;
		case KEY_F6: return Rml::Input::KI_F6;
		case KEY_F7: return Rml::Input::KI_F7;
		case KEY_F8: return Rml::Input::KI_F8;
		case KEY_F9: return Rml::Input::KI_F9;
		case KEY_F10: return Rml::Input::KI_F10;
		case KEY_F11: return Rml::Input::KI_F11;
		case KEY_F12: return Rml::Input::KI_F12;
		default: return Rml::Input::KI_UNKNOWN;
	}
}

struct RmlUiEventBridge : public Rml::EventListener {
	Value callback = Value::null;
	Interpreter* interpreter = nullptr;

	explicit RmlUiEventBridge(Value cb, Interpreter* interp) : callback(cb), interpreter(interp) {}

	void ProcessEvent(Rml::Event& event) override {
		if (callback.type != ValueType::Function || interpreter == nullptr || interpreter->vm == nullptr) return;
		Machine* vm = interpreter->vm;
		Context* callerContext = vm->GetTopContext();
		if (callerContext == nullptr) return;

		ValueDict eventMap;
		eventMap.SetValue(String("type"), Value(String(event.GetType().c_str())));
		eventMap.SetValue(String("targetElement"), RmlUiElementToValue(event.GetTargetElement()));

		ValueDict params;
		Rml::Vector2f mousePos = event.GetUnprojectedMouseScreenPos();
		params.SetValue(String("x"), Value((double)mousePos.x));
		params.SetValue(String("y"), Value((double)mousePos.y));
		eventMap.SetValue(String("parameters"), Value(params));

		ValueList args;
		args.Add(Value(eventMap));

		FunctionStorage* invoker = new FunctionStorage();
		invoker->code.Add(TACLine(TACLine::Op::PushParam, args[0]));
		invoker->code.Add(TACLine(Value::Temp(0), TACLine::Op::CallFunctionA, callback, Value(1)));
		Value previousTemp0 = callerContext->GetTemp(0, Value::null);
		vm->ManuallyPushCall(invoker, Value::Temp(0));
		delete invoker;
		try {
			while (vm->GetTopContext() != callerContext && !vm->yielding) vm->Step();
		} catch (...) {
		}
		callerContext->SetTemp(0, previousTemp0);
	}
};

void RemoveElementListeners(Rml::Element* element) {
	auto it = g_rmlui.listenersByElement.find(element);
	if (it == g_rmlui.listenersByElement.end()) return;
	for (ElementListenerBinding& binding : it->second) {
		if (binding.listener) {
			element->RemoveEventListener(binding.eventType.c_str(), binding.listener);
			delete binding.listener;
		}
	}
	g_rmlui.listenersByElement.erase(it);
}

}
#endif

void ResetRmlUiBridge() {
#ifdef HAVE_RMLUI
	if (!g_rmlui.initialized) return;

	for (auto& pair : g_rmlui.listenersByElement) {
		Rml::Element* element = pair.first;
		for (ElementListenerBinding& binding : pair.second) {
			if (binding.listener) {
				element->RemoveEventListener(binding.eventType.c_str(), binding.listener);
				delete binding.listener;
			}
		}
	}
	g_rmlui.listenersByElement.clear();

	std::vector<std::string> names;
	names.reserve(g_rmlui.contextNames.size());
	for (const auto& pair : g_rmlui.contextNames) names.push_back(pair.second);
	for (const std::string& name : names) {
		Rml::RemoveContext(name.c_str());
	}
	g_rmlui.contextNames.clear();

	if (g_rmlui.renderInterface) {
		g_rmlui.renderInterface->ReleaseAllTextures();
	}

	Rml::Shutdown();
	g_rmlui.renderInterface.reset();
	g_rmlui.systemInterface.reset();
	g_rmlui.initialized = false;
#endif
}

void AddRRmlUiMethods(ValueDict raylibModule) {
	Intrinsic* i;

	i = Intrinsic::Create("");
	i->AddParam("width", Value(960));
	i->AddParam("height", Value(640));
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		if (!EnsureRmlUiInitialized()) return IntrinsicResult::Null;

		int width = context->GetVar(String("width")).IntValue();
		int height = context->GetVar(String("height")).IntValue();
		if (width <= 0) width = 1;
		if (height <= 0) height = 1;

		char nameBuf[64];
		snprintf(nameBuf, sizeof(nameBuf), "ms_ctx_%d", (int)Rml::GetNumContexts() + 1);
		Rml::Context* ctx = Rml::CreateContext(nameBuf, Rml::Vector2i(width, height));
		if (ctx == nullptr) return IntrinsicResult::Null;
		g_rmlui.contextNames[ctx] = std::string(nameBuf);
		return IntrinsicResult(RmlUiContextToValue(ctx));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiCreateContext", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		auto it = g_rmlui.contextNames.find(ctx);
		if (it != g_rmlui.contextNames.end()) {
			Rml::RemoveContext(it->second.c_str());
			g_rmlui.contextNames.erase(it);
		}
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiUnloadContext", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("width");
	i->AddParam("height");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		int width = context->GetVar(String("width")).IntValue();
		int height = context->GetVar(String("height")).IntValue();
		if (width <= 0) width = 1;
		if (height <= 0) height = 1;
		ctx->SetDimensions(Rml::Vector2i(width, height));
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiSetContextDimensions", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("fontPath");
	i->AddParam("fallback", Value::zero);
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		if (!EnsureRmlUiInitialized()) return IntrinsicResult(false);
		String fontPath = context->GetVar(String("fontPath")).ToString();
		bool fallback = context->GetVar(String("fallback")).BoolValue();
		return IntrinsicResult(Rml::LoadFontFace(fontPath.c_str(), fallback));
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiLoadFontFace", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("path");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		String path = context->GetVar(String("path")).ToString();
		Rml::ElementDocument* doc = ctx->LoadDocument(path.c_str());
		if (doc == nullptr) return IntrinsicResult::Null;
		return IntrinsicResult(RmlUiDocumentToValue(doc));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiLoadDocument", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("rml");
	i->AddParam("source", Value(String("[document from miniscript]")));
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		String rml = context->GetVar(String("rml")).ToString();
		String source = context->GetVar(String("source")).ToString();
		Rml::ElementDocument* doc = ctx->LoadDocumentFromMemory(rml.c_str(), source.c_str());
		if (doc == nullptr) return IntrinsicResult::Null;
		return IntrinsicResult(RmlUiDocumentToValue(doc));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiLoadDocumentFromString", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("id");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		String id = context->GetVar(String("id")).ToString();
		return IntrinsicResult(RmlUiDocumentToValue(ctx->GetDocument(id.c_str())));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiGetDocumentById", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (ctx == nullptr || doc == nullptr) return IntrinsicResult::Null;
		ctx->UnloadDocument(doc);
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiUnloadDocument", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc) doc->Show();
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiShowDocument", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc) doc->Hide();
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiHideDocument", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc == nullptr) return IntrinsicResult(false);
		return IntrinsicResult(doc->Focus());
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiFocusDocument", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc == nullptr) return IntrinsicResult(false);
		return IntrinsicResult(doc->IsVisible(true));
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiIsDocumentVisible", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		ctx->Update();
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiUpdateContext", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("target", Value::null);
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult::Null;
		Value targetVal = context->GetVar(String("target"));
		if (targetVal.IsNull()) {
			ctx->Render();
			return IntrinsicResult::Null;
		}

		RenderTexture2D target = ValueToRenderTexture(targetVal);
		BeginTextureMode(target);
		ClearBackground((Color){0, 0, 0, 0});
		ctx->Render();
		EndTextureMode();
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiRenderContext", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("x");
	i->AddParam("y");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult(false);
		int x = context->GetVar(String("x")).IntValue();
		int y = context->GetVar(String("y")).IntValue();
		bool unhandled = ctx->ProcessMouseMove(x, y, CurrentKeyModifiers());
		return IntrinsicResult(!unhandled);
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiProcessMouseMove", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("button");
	i->AddParam("down");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult(false);
		int button = context->GetVar(String("button")).IntValue();
		bool down = context->GetVar(String("down")).BoolValue();
		bool unhandled = down ?
			ctx->ProcessMouseButtonDown(button, CurrentKeyModifiers()) :
			ctx->ProcessMouseButtonUp(button, CurrentKeyModifiers());
		return IntrinsicResult(!unhandled);
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiProcessMouseButton", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("deltaX", Value::zero);
	i->AddParam("deltaY", Value::zero);
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult(false);
		float dx = context->GetVar(String("deltaX")).FloatValue();
		float dy = context->GetVar(String("deltaY")).FloatValue();
		bool unhandled = ctx->ProcessMouseWheel(Rml::Vector2f(dx, dy), CurrentKeyModifiers());
		return IntrinsicResult(!unhandled);
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiProcessMouseWheel", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("key");
	i->AddParam("down", Value(1));
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult(false);
		int key = context->GetVar(String("key")).IntValue();
		bool down = context->GetVar(String("down")).BoolValue();
		Rml::Input::KeyIdentifier k = RaylibKeyToRmlKey(key);
		bool unhandled = down ? ctx->ProcessKeyDown(k, CurrentKeyModifiers()) : ctx->ProcessKeyUp(k, CurrentKeyModifiers());
		return IntrinsicResult(!unhandled);
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiProcessKeyboardEvent", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("context");
	i->AddParam("text");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Context* ctx = ValueToRmlUiContext(context->GetVar(String("context")));
		if (ctx == nullptr) return IntrinsicResult(false);
		String text = context->GetVar(String("text")).ToString();
		bool unhandled = ctx->ProcessTextInput(text.c_str());
		return IntrinsicResult(!unhandled);
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiProcessTextInput", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->AddParam("id");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc == nullptr) return IntrinsicResult::Null;
		String id = context->GetVar(String("id")).ToString();
		return IntrinsicResult(RmlUiElementToValue(doc->GetElementById(id.c_str())));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiGetElementById", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("document");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::ElementDocument* doc = ValueToRmlUiDocument(context->GetVar(String("document")));
		if (doc == nullptr) return IntrinsicResult::Null;
		return IntrinsicResult(RmlUiElementToValue(doc));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiGetDocumentRoot", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("element");
	i->AddParam("property");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Element* el = ValueToRmlUiElement(context->GetVar(String("element")));
		if (el == nullptr) return IntrinsicResult::Null;
		String property = context->GetVar(String("property")).ToString();
		const Rml::Property* p = el->GetProperty(property.c_str());
		if (p == nullptr) return IntrinsicResult::Null;
		return IntrinsicResult(Value(String(p->ToString().c_str())));
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiGetElementProperty", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("element");
	i->AddParam("property");
	i->AddParam("value");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Element* el = ValueToRmlUiElement(context->GetVar(String("element")));
		if (el == nullptr) return IntrinsicResult(false);
		String property = context->GetVar(String("property")).ToString();
		String value = context->GetVar(String("value")).ToString();
		return IntrinsicResult(el->SetProperty(property.c_str(), value.c_str()));
#else
		(void)context;
		return IntrinsicResult(false);
#endif
	};
	raylibModule.SetValue("RmlUiSetElementProperty", i->GetFunc());

	i = Intrinsic::Create("");
	i->AddParam("element");
	i->AddParam("eventType");
	i->AddParam("callback");
	i->code = INTRINSIC_LAMBDA {
#ifdef HAVE_RMLUI
		Rml::Element* element = ValueToRmlUiElement(context->GetVar(String("element")));
		if (element == nullptr) return IntrinsicResult::Null;
		String eventType = context->GetVar(String("eventType")).ToString();
		Value callback = context->GetVar(String("callback"));

		if (!(callback.IsNull() || callback.type == ValueType::Function)) {
			TraceLog(LOG_ERROR, "RmlUiSetElementEventListener: callback must be function or null.");
			return IntrinsicResult::Null;
		}

		if (callback.IsNull()) {
			RemoveElementListeners(element);
			return IntrinsicResult::Null;
		}

		if (context->vm == nullptr || context->vm->GetTopContext() == nullptr) {
			TraceLog(LOG_ERROR, "RmlUiSetElementEventListener: interpreter VM not available.");
			return IntrinsicResult::Null;
		}

		Interpreter* interp = context->vm->interpreter;
		if (interp == nullptr) {
			TraceLog(LOG_ERROR, "RmlUiSetElementEventListener: interpreter not available.");
			return IntrinsicResult::Null;
		}

		auto* listener = new RmlUiEventBridge(callback, interp);
		element->AddEventListener(eventType.c_str(), listener);
		ElementListenerBinding binding;
		binding.listener = listener;
		binding.eventType = eventType.c_str();
		g_rmlui.listenersByElement[element].push_back(binding);
		return IntrinsicResult::Null;
#else
		(void)context;
		return IntrinsicResult::Null;
#endif
	};
	raylibModule.SetValue("RmlUiSetElementEventListener", i->GetFunc());
}
