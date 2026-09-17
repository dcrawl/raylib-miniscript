// Placeholder MiniScript-side test, proving the headless test-mode pipeline
// works end to end. Replace/extend with real game-logic assert scripts
// (combat queue resolution, movement queue lazy validation, dialogue pause)
// once that code exists.

import "assert"

assert.assertEqual(1 + 1, 2, "basic arithmetic")
assert.assertEqual("ember" + "fall", "emberfall", "string concatenation")
assert.assertTrue(not raylib.IsWindowReady, "no window was created for a headless test run")

assert.reportAndExit
