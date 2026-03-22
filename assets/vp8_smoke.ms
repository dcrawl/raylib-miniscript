// Video smoke test for the initial video API.
// Desktop currently expects IVF/VP8 through libvpx; web can use .webm via browser media stack.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	video = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if video == null then
	print "LoadVideoStream failed. Expected VP8 in WebM or IVF."
	print "video error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
print "video backend=" + rl.GetVideoBackend(video)
print "video meta: container=" + meta.container + " codec=" + meta.codec + " fps=" + str(meta.frameRate) + " length=" + str(meta.timeLength) + " rate=" + str(meta.playbackRate) + " looping=" + str(meta.looping)

// Start non-looping by default so DidVideoFinish can be seen quickly.
rl.SetVideoLooping video, 0
looping = 0
rate = rl.GetVideoPlaybackRate(video)

if meta.frameRate > 0 then
	rl.SetTargetFPS meta.frameRate
else
	rl.SetTargetFPS 60
end if
startPos = meta.timeLength - 3.0
if startPos < 0 then startPos = 0
rl.SeekVideoStream video, startPos
rl.PlayVideoStream video
finishCount = 0
loopCount = 0
startTime = rl.GetTime
timeoutSec = 20

while not rl.WindowShouldClose
	// Keep interpreter responsive by decoding incrementally each frame.
	rl.UpdateVideoStream video

	if rl.IsKeyPressed(rl.KEY_ESCAPE) then
		rl.StopVideoStream video
		break
	end if

	if rl.IsKeyPressed(rl.KEY_RIGHT) then
		rl.SeekVideoStream video, rl.GetVideoTimePlayed(video) + 5
	end if

	if rl.IsKeyPressed(rl.KEY_L) then
		looping = 1 - looping
		rl.SetVideoLooping video, looping
		print "looping=" + str(looping)
	end if

	if rl.IsKeyPressed(rl.KEY_UP) then
		rate = rate + 0.25
		rl.SetVideoPlaybackRate video, rate
		rate = rl.GetVideoPlaybackRate(video)
		print "playbackRate=" + str(rate)
	end if

	if rl.IsKeyPressed(rl.KEY_DOWN) then
		rate = rate - 0.25
		rl.SetVideoPlaybackRate video, rate
		rate = rl.GetVideoPlaybackRate(video)
		print "playbackRate=" + str(rate)
	end if

	if rl.DidVideoLoop(video) then
		loopCount = loopCount + 1
		print "DidVideoLoop fired"
	end if

	if rl.DidVideoFinish(video) then
		finishCount = finishCount + 1
		print "DidVideoFinish fired"
		if not looping then break
	end if

	if rl.GetTime - startTime > timeoutSec then
		print "VP8 smoke timeout reached"
		print "video error=" + rl.GetVideoLastError(video)
		break
	end if

	tex = rl.GetVideoTexture(video)

	rl.BeginDrawing
	rl.ClearBackground rl.BLACK
	rl.DrawTexture tex, 0, 0, rl.WHITE
	rl.DrawText "ESC stop  RIGHT seek+5s  L loop on/off", 20, 20, 20, rl.YELLOW
	rl.DrawText "UP/DOWN playback rate  hook logs in console", 20, 46, 20, rl.YELLOW
	rl.EndDrawing
end while

rl.UnloadVideoStream video
if not looping and finishCount < 1 then
	print "VP8 smoke failed: DidVideoFinish not observed"
	print "video error=" + rl.GetVideoLastError(video)
	exit 1
end if
print "VP8 smoke done: finish=" + str(finishCount) + " loop=" + str(loopCount)
