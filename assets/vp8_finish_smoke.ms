// Finish-event smoke test for video hooks.
// Focused on DidVideoFinish with non-looping playback.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	video = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if video == null then
	print "LoadVideoStream failed. Expected VP8 in WebM or IVF."
	exit 1
end if

meta = rl.GetVideoInfo(video)
print "finish smoke backend=" + rl.GetVideoBackend(video)
print "finish smoke fps=" + str(meta.frameRate) + " length=" + str(meta.timeLength)

if meta.frameRate > 0 then
	rl.SetTargetFPS meta.frameRate
else
	rl.SetTargetFPS 60
end if

// Non-looping path for finish hook.
rl.SetVideoLooping video, 0
rl.SetVideoPlaybackRate video, 1.5

// Seek near end so finish event happens quickly.
startPos = meta.timeLength - 1.0
if startPos < 0 then startPos = 0
rl.SeekVideoStream video, startPos

// Clear stale edge flags before entering the loop.
ignore = rl.DidVideoFinish(video)
ignore = rl.DidVideoLoop(video)

rl.PlayVideoStream video
finishCount = 0
startTime = rl.GetTime
timeoutSec = 12

while not rl.WindowShouldClose
	rl.UpdateVideoStream video

	if rl.DidVideoFinish(video) then
		finishCount = finishCount + 1
		print "DidVideoFinish fired count=" + str(finishCount)
		break
	end if

	if rl.DidVideoLoop(video) then
		print "DidVideoLoop fired (unexpected in non-looping test)"
	end if

	if rl.GetTime - startTime > timeoutSec then
		print "Finish smoke timeout reached"
		break
	end if

	tex = rl.GetVideoTexture(video)
	rl.BeginDrawing
	rl.ClearBackground rl.BLACK
	rl.DrawTexture tex, 0, 0, rl.WHITE
	rl.DrawText "Finish smoke: waiting for DidVideoFinish", 20, 20, 20, rl.YELLOW
	rl.EndDrawing
end while

rl.StopVideoStream video
rl.UnloadVideoStream video
print "Finish smoke done: finishes=" + str(finishCount)
