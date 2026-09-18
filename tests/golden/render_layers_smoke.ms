// Golden-image regression test for render_layers.ms.
//
// Draws a known pattern into the low-res viewport layer and a known pattern
// into the high-res UI layer, composites them (viewport first, UI over it),
// exports the composited frame, and diffs it against a checked-in reference
// image. Catches regressions in the integer-scale/composite math and in the
// point-filtering enforcement -- both layers are explicitly forced to
// TEXTURE_FILTER_POINT inside RenderLayers.init.
//
// Run it with:
//
//   ./build/raylib-miniscript tests/golden/render_layers_smoke.ms
//
// A missing reference image is created on first run rather than treated as
// a failure -- review tests/golden/reference/render_layers_smoke.png
// yourself once, then commit it.

import "render_layers"

screenWidth = 320
screenHeight = 180

raylib.SetConfigFlags(raylib.FLAG_WINDOW_HIDDEN)
raylib.InitWindow(screenWidth, screenHeight, "render-layers-golden-smoke")

layers = render_layers.RenderLayers.make
// Use a small UI resolution for this test so both layers are visible (and
// at different integer scale factors) within a small, fast-to-diff capture
// -- the real game/editor use the module's normal 1280x720 UI default.
layers.uiWidth = 160
layers.uiHeight = 90
layers.init

raylib.BeginTextureMode(layers.viewportTarget)
raylib.ClearBackground(raylib.DARKBLUE)
raylib.DrawRectangle(8, 8, 16, 16, raylib.ORANGE)
raylib.EndTextureMode

raylib.BeginTextureMode(layers.uiTarget)
raylib.ClearBackground(raylib.BLANK)
raylib.DrawRectangle(40, 40, 80, 30, raylib.RAYWHITE)
raylib.EndTextureMode

captureTarget = raylib.LoadRenderTexture(screenWidth, screenHeight)
raylib.SetTextureFilter(captureTarget.texture, raylib.TEXTURE_FILTER_POINT)

raylib.BeginTextureMode(captureTarget)
raylib.ClearBackground(raylib.BLACK)
layers.composite(screenWidth, screenHeight)
raylib.EndTextureMode

image = raylib.LoadImageFromTexture(captureTarget.texture)

refPath = "tests/golden/reference/render_layers_smoke.png"
outPath = "tests/golden/output/render_layers_smoke.png"
raylib.ExportImage(image, outPath)

if not raylib.FileExists(refPath) then
	raylib.ExportImage(image, refPath)
	print "No reference image found; created " + refPath + " -- review it, then commit."
	raylib.CloseWindow
	exit(0)
end if

reference = raylib.LoadImage(refPath)

mismatches = 0
firstMismatch = null

for y in range(0, screenHeight - 1)
	for x in range(0, screenWidth - 1)
		actual = raylib.GetImageColor(image, x, y)
		expected = raylib.GetImageColor(reference, x, y)
		if actual != expected then
			mismatches = mismatches + 1
			if firstMismatch == null then firstMismatch = [x, y]
		end if
	end for
end for

raylib.CloseWindow

if mismatches == 0 then
	print "golden image match: " + refPath
	exit(0)
else
	print "golden image MISMATCH: " + mismatches + " pixel(s) differ, first at " + firstMismatch
	print "if this is an intentional rendering change, review " + outPath + " and replace " + refPath
	exit(1)
end if
