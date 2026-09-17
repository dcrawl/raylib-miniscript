//
//  ember_tests.cpp
//  raylib-miniscript (EmberEngine preproduction scaffold)
//
//  Placeholder for EmberEngine's own C++ engine/binding unit tests, per
//  CLAUDE.md's testing strategy: extend this tests/ folder rather than
//  reinvent one. Follows the hand-rolled assert/printf style of
//  fs_tests.cpp and interp_tests.cpp (no gtest/catch2) so all three build
//  and run the same way. There is no EmberEngine-specific engine code yet;
//  this proves the harness itself -- build target, MiniScript linkage, exit
//  code convention -- works before any real test is written against it.
//
//  Build:  cmake --build build --target ember_tests
//  Run:    ./build/ember_tests
//

#include "miniscript.h"

#include <stdio.h>

using namespace MiniScript;

static int failures = 0;
static int checks = 0;

static void ok(bool condition, const char* what) {
	checks++;
	if (condition) {
		printf("ok   %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static void testHarnessLinksAgainstMiniScript() {
	Value a(2.0);
	Value b(2.0);
	ok(a == b, "MiniScript Value equality works from an EmberEngine test binary");
}

int main() {
	testHarnessLinksAgainstMiniScript();

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
