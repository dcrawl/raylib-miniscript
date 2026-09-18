// Exercises render_layers.ms: render-texture creation/validity and the
// integer-scale arithmetic composite() relies on. The actual composited
// pixels are covered separately by tests/golden/render_layers_smoke.ms --
// this test covers the parts that don't need a pixel diff.

import "assert"
import "render_layers"

// Render textures are GPU resources, so this test needs a real (hidden)
// window, same as tests/golden/*.ms -- still headless, nothing is shown.
raylib.SetConfigFlags(raylib.FLAG_WINDOW_HIDDEN)
raylib.InitWindow(64, 64, "render-layers-test")

layers = render_layers.RenderLayers.make
layers.init

assert.assertTrue(raylib.IsRenderTextureValid(layers.viewportTarget), "viewport render texture is valid after init")
assert.assertTrue(raylib.IsRenderTextureValid(layers.uiTarget), "UI render texture is valid after init")
assert.assertEqual(layers.viewportTarget.texture.width, layers.internalWidth, "viewport texture width matches internalWidth")
assert.assertEqual(layers.viewportTarget.texture.height, layers.internalHeight, "viewport texture height matches internalHeight")
assert.assertEqual(layers.uiTarget.texture.width, layers.uiWidth, "UI texture width matches uiWidth")
assert.assertEqual(layers.uiTarget.texture.height, layers.uiHeight, "UI texture height matches uiHeight")

// integerScaleFor: largest integer multiple of (baseWidth, baseHeight) that
// still fits within (screenWidth, screenHeight), never below 1x.
assert.assertEqual(layers.integerScaleFor(320, 180, 1920, 1080), 6, "320x180 into 1920x1080 scales to 6x")
assert.assertEqual(layers.integerScaleFor(320, 180, 1280, 720), 4, "320x180 into 1280x720 scales to 4x")
assert.assertEqual(layers.integerScaleFor(320, 180, 640, 360), 2, "320x180 into 640x360 scales to 2x")
assert.assertEqual(layers.integerScaleFor(320, 180, 100, 100), 1, "never scales below 1x even when the target is smaller")
assert.assertEqual(layers.integerScaleFor(384, 216, 1920, 1080), 5, "384x216 into 1920x1080 scales to 5x")
assert.assertEqual(layers.integerScaleFor(480, 270, 1920, 1080), 4, "480x270 into 1920x1080 scales to 4x")
assert.assertEqual(layers.integerScaleFor(640, 360, 1920, 1080), 3, "640x360 into 1920x1080 scales to 3x")
// Width and height are constrained independently -- the narrower dimension
// caps the scale, so a mismatched aspect ratio doesn't overflow the screen.
assert.assertEqual(layers.integerScaleFor(320, 180, 1920, 200), 1, "the tighter dimension caps the scale factor")

layers.unload
raylib.CloseWindow

assert.reportAndExit
