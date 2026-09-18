// render_layers.ms
//
// The engine's two independent, integer-scaled render layers, composited
// together: a low-res 3D viewport for the chunky pixel look, and a
// separate, higher-res UI layer using bitmap fonts. See the design doc's
// Art Direction section and CLAUDE.md's "Two independent render layers"
// rule -- this module is the one place that rule is implemented, so every
// scene (game or editor) should draw through it rather than composing its
// own render textures.
//
// Both layers are point-filtered explicitly: nearest-neighbor/point
// filtering only, no mipmapping/bilinear/trilinear, is a hard renderer
// requirement that silently breaks the whole art pipeline if missed.
//
// Usage:
//   import "render_layers"
//   layers = render_layers.RenderLayers.make
//   layers.init
//   ... each frame ...
//   raylib.BeginTextureMode(layers.viewportTarget)
//     ... draw the 3D scene at layers.internalWidth x layers.internalHeight ...
//   raylib.EndTextureMode
//   raylib.BeginTextureMode(layers.uiTarget)
//     ... draw UI text/icons at layers.uiWidth x layers.uiHeight ...
//   raylib.EndTextureMode
//   raylib.BeginDrawing
//     layers.composite(raylib.GetScreenWidth, raylib.GetScreenHeight)
//   raylib.EndDrawing

RenderLayers = {}

// Internal 3D viewport resolution. 320x180 is the design doc's current
// hypothesis; the integer-scalable fallbacks are 384x216, 480x270, 640x360.
// Swap these two fields to try a different candidate during validation.
RenderLayers.internalWidth = 320
RenderLayers.internalHeight = 180

// UI layer's own, independent (higher) resolution.
RenderLayers.uiWidth = 1280
RenderLayers.uiHeight = 720

RenderLayers.viewportTarget = null
RenderLayers.uiTarget = null

RenderLayers.make = function
	result = new RenderLayers
	return result
end function

// Creates both render textures and enforces point filtering on each.
// Must be called once, after InitWindow, before the first frame.
RenderLayers.init = function
	self.viewportTarget = raylib.LoadRenderTexture(self.internalWidth, self.internalHeight)
	raylib.SetTextureFilter(self.viewportTarget.texture, raylib.TEXTURE_FILTER_POINT)

	self.uiTarget = raylib.LoadRenderTexture(self.uiWidth, self.uiHeight)
	raylib.SetTextureFilter(self.uiTarget.texture, raylib.TEXTURE_FILTER_POINT)
end function

RenderLayers.unload = function
	if self.viewportTarget != null then raylib.UnloadRenderTexture(self.viewportTarget)
	if self.uiTarget != null then raylib.UnloadRenderTexture(self.uiTarget)
	self.viewportTarget = null
	self.uiTarget = null
end function

// The largest integer multiple of (baseWidth, baseHeight) that still fits
// within (screenWidth, screenHeight). Never scales up by a fraction, and
// never scales below 1x, so pixels always land exactly on the display grid.
RenderLayers.integerScaleFor = function(baseWidth, baseHeight, screenWidth, screenHeight)
	scaleX = floor(screenWidth / baseWidth)
	scaleY = floor(screenHeight / baseHeight)
	scale = scaleX
	if scaleY < scale then scale = scaleY
	if scale < 1 then scale = 1
	return scale
end function

// Draws one render-texture target into the current draw target (window or
// another render texture), integer-scaled and centered within
// (screenWidth, screenHeight).
//
// Render-texture textures are stored bottom-up (OpenGL convention), so the
// source rect's height is negated to flip them back right-side-up -- a
// well-known Raylib gotcha, not a bug in this module.
RenderLayers.blit = function(target, screenWidth, screenHeight)
	tex = target.texture
	scale = self.integerScaleFor(tex.width, tex.height, screenWidth, screenHeight)
	destWidth = tex.width * scale
	destHeight = tex.height * scale
	destX = (screenWidth - destWidth) / 2
	destY = (screenHeight - destHeight) / 2

	source = [0, 0, tex.width, -tex.height]
	dest = [destX, destY, destWidth, destHeight]
	origin = [0, 0]
	raylib.DrawTexturePro(tex, source, dest, origin, 0, raylib.WHITE)
end function

// Composites both layers to the current draw target: viewport first, UI
// layer drawn over it. Call this between BeginDrawing/EndDrawing.
RenderLayers.composite = function(screenWidth, screenHeight)
	self.blit(self.viewportTarget, screenWidth, screenHeight)
	self.blit(self.uiTarget, screenWidth, screenHeight)
end function

if locals == globals then
	print "render_layers.ms is a library module; import it, don't run it directly."
end if
