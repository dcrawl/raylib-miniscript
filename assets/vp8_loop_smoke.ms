// Loop-event smoke test for video hooks.
// Demonstrates DidVideoLoop with automatic short run and clean exit.

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
print "loop smoke backend=" + rl.GetVideoBackend(video)
print "loop smoke fps=" + str(meta.frameRate) + " length=" + str(meta.timeLength)

if meta.frameRate > 0 then
	rl.SetTargetFPS meta.frameRate
else
	rl.SetTargetFPS 60
end if

// Force loop quickly: loop enabled, start near end, and run at 2x speed.
rl.SetVideoLooping video, 1
rl.SetVideoPlaybackRate video, 2.0
startPos = meta.timeLength - 1.0
if startPos < 0 then startPos = 0
rl.SeekVideoStream video, startPos
rl.PlayVideoStream video

// Clear any stale edge flags before entering the loop.
ignore = rl.DidVideoLoop(video)
ignore = rl.DidVideoFinish(video)

loopCount = 0
finishCount = 0
maxLoops = 2
startTime = rl.GetTime
timeoutSec = 15

while not rl.WindowShouldClose
	rl.UpdateVideoStream video

	if rl.DidVideoLoop(video) then
		loopCount = loopCount + 1
		print "DidVideoLoop fired count=" + str(loopCount)
		if loopCount >= maxLoops then
			break
		end if
	end if

	if rl.DidVideoFinish(video) then
		finishCount = finishCount + 1
		print "DidVideoFinish fired count=" + str(finishCount)
	end if

	if rl.GetTime - startTime > timeoutSec then
		print "Loop smoke timeout reached"
		break
	end if

	tex = rl.GetVideoTexture(video)
	rl.BeginDrawing
	rl.ClearBackground rl.BLACK
	rl.DrawTexture tex, 0, 0, rl.WHITE
	rl.DrawText "Loop smoke: waiting for 2 loop events", 20, 20, 20, rl.YELLOW
	rl.EndDrawing
end while

rl.StopVideoStream video
rl.UnloadVideoStream video
print "Loop smoke done: loops=" + str(loopCount) + " finishes=" + str(finishCount)
