//
//  RaylibIntrinsics.h
//  raylib-miniscript
//
//  Raylib intrinsics for MiniScript
//

#ifndef RAYLIBINTRINSICS_H
#define RAYLIBINTRINSICS_H

namespace MiniScript {
	class Interpreter;
}

// Add Raylib intrinsics to the global state
void AddRaylibIntrinsics();

// Clear native callback hooks and callback bridge state.
void ResetRaylibCallbackBridge();

// Shutdown and clear RmlUi bridge state.
void ResetRmlUiBridge();

#endif // RAYLIBINTRINSICS_H
