// VP8 CI smoke: start near EOF so finish is observed quickly.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	video = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if video == null then
	print "CI VP8 smoke failed: LoadVideoStream failed. Expected VP8 in WebM or IVF."
	print "video error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
print "ci smoke backend=" + rl.GetVideoBackend(video)
print "ci smoke meta: container=" + meta.container + " codec=" + meta.codec + " fps=" + str(meta.frameRate) + " length=" + str(meta.timeLength)

rl.SetVideoLooping video, 0
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
startTime = rl.GetTime
timeoutSec = 20

while not rl.WindowShouldClose
	rl.UpdateVideoStream video
	if rl.DidVideoFinish(video) then
		finishCount = finishCount + 1
		print "DidVideoFinish fired"
		break
	end if
	if rl.GetTime - startTime > timeoutSec then
		print "CI VP8 smoke timeout reached"
		print "video error=" + rl.GetVideoLastError(video)
		break
	end if

	tex = rl.GetVideoTexture(video)
	rl.BeginDrawing
	rl.ClearBackground rl.BLACK
	rl.DrawTexture tex, 0, 0, rl.WHITE
	rl.EndDrawing
end while

timingDiag = rl.GetVideoFrameTimingDiagnostics(video)
rl.UnloadVideoStream video
if finishCount < 1 then
	print "CI VP8 smoke failed: DidVideoFinish not observed"
	print "video error=" + rl.GetVideoLastError(video)
	exit 1
end if
if timingDiag == null or timingDiag.totalFramesDecoded < 1 then
	print "CI VP8 smoke failed: timing diagnostics invalid"
	exit 1
end if
print "CI VP8 smoke done: finish=" + str(finishCount)
