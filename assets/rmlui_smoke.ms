// rmlui_smoke.ms
// Verifies initial RmlUi integration with raylib-miniscript.

rl = raylib

ctx = ui.RmlUiCreateContext(960, 640)
if ctx == null then
	print "RmlUi context creation failed."
	x = null[0]
end if

fontLoaded = ui.RmlUiLoadFontFace("assets/EquipmentPro.ttf")
if not fontLoaded then
	print "Warning: RmlUi font load failed (text may not render as expected)."
end if

doc = ui.RmlUiLoadDocument(ctx, "assets/rmlui_smoke.rml")
if doc == null then
	print "RmlUi document load failed."
	x = null[0]
end if

ui.RmlUiShowDocument(doc)

btn = ui.RmlUiGetElementById(doc, "btn")
if btn != null then
	onClick = function(event)
		print "RmlUi click event: " + event.type
		ui.RmlUiSetElementProperty(btn, "background-color", "#2e8f58")
	end function
	ui.RmlUiSetElementEventListener(btn, "click", @onClick)
end if

uiTarget = rl.LoadRenderTexture(960, 640)

while true
	ui.RmlUiProcessMouseMove(ctx, rl.GetMouseX, rl.GetMouseY)
	if rl.IsMouseButtonPressed(rl.MOUSE_BUTTON_LEFT) then ui.RmlUiProcessMouseButton(ctx, 0, true)
	if rl.IsMouseButtonReleased(rl.MOUSE_BUTTON_LEFT) then ui.RmlUiProcessMouseButton(ctx, 0, false)

	wheel = rl.GetMouseWheelMove
	if wheel != 0 then ui.RmlUiProcessMouseWheel(ctx, 0, wheel)

	key = rl.GetKeyPressed
	while key != 0
		ui.RmlUiProcessKeyboardEvent(ctx, key, true)
		ui.RmlUiProcessKeyboardEvent(ctx, key, false)
		key = rl.GetKeyPressed
	end while

	ui.RmlUiUpdateContext(ctx)

	rl.BeginDrawing
	rl.ClearBackground([20, 24, 36, 255])

	ui.RmlUiRenderContext(ctx, uiTarget)

	src = [0, 0, uiTarget.texture.width, -uiTarget.texture.height]
	rl.DrawTextureRec(uiTarget.texture, src, [0, 0], rl.WHITE)
	rl.DrawText("Bitmap font ladder: choose the sharpest row (ESC to quit)", 20, 606, 18, rl.RAYWHITE)

	rl.EndDrawing

	if rl.IsKeyPressed(rl.KEY_ESCAPE) then break
	yield
end while

ui.RmlUiUnloadContext(ctx)
rl.UnloadRenderTexture(uiTarget)
