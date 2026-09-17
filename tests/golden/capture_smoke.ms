// Golden-image regression smoke test.
//
// Proves the render -> capture -> diff pipeline works headlessly, before any
// real dungeon geometry exists to test. Renders a tiny known scene to a
// render texture, exports it, and compares it pixel-for-pixel against a
// checked-in reference image. Run it with:
//
//   ./build/raylib-miniscript tests/golden/capture_smoke.ms
//
// A missing reference image is created on first run rather than treated as
// a failure, so a fresh checkout doesn't need one committed by hand -- but
// that means the FIRST run of a NEW scene always "passes" trivially; review
// tests/golden/reference/smoke.png yourself once, then commit it.
//
// Nearest-neighbor filtering is set explicitly because that's a project-wide
// renderer requirement (see CLAUDE.md) that this test exists partly to guard.

raylib.SetConfigFlags(raylib.FLAG_WINDOW_HIDDEN)
raylib.InitWindow(64, 64, "golden-smoke")

target = raylib.LoadRenderTexture(64, 64)
raylib.SetTextureFilter(target.texture, raylib.TEXTURE_FILTER_POINT)

raylib.BeginTextureMode(target)
raylib.ClearBackground(raylib.BLUE)
raylib.DrawRectangle(16, 16, 32, 32, raylib.RED)
raylib.EndTextureMode

image = raylib.LoadImageFromTexture(target.texture)

refPath = "tests/golden/reference/smoke.png"
outPath = "tests/golden/output/smoke.png"
raylib.ExportImage(image, outPath)

if not raylib.FileExists(refPath) then
	raylib.ExportImage(image, refPath)
	print "No reference image found; created " + refPath + " -- review it, then commit."
	raylib.CloseWindow
	exit(0)
end if

reference = raylib.LoadImage(refPath)

width = 64
height = 64
mismatches = 0
firstMismatch = null

for y in range(0, height - 1)
	for x in range(0, width - 1)
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
