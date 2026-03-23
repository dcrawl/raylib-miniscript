// Verify desktop seek playback realigns audio session position and skew diagnostics.

rl = raylib
statusCodes = {"ok":"ok"}

video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	video = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if video == null then
	print "Seek audio align smoke failed: LoadVideoStream failed"
	print "video error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
if meta == null then
	print "Seek audio align smoke failed: GetVideoInfo returned null"
	rl.UnloadVideoStream video
	exit 1
end if

if meta.frameRate > 0 then
	rl.SetTargetFPS meta.frameRate
else
	rl.SetTargetFPS 60
end if

seekPos = 5
if meta.timeLength > 8 then seekPos = meta.timeLength * 0.35
if seekPos < 1 then seekPos = 1

rl.SeekVideoStream video, seekPos
rl.PlayVideoStream video

startTime = rl.GetTime
diag = null
queue = null
syncTune = null
while rl.GetTime - startTime < 0.35
	rl.UpdateVideoStream video
	diag = rl.GetVideoFrameTimingDiagnostics(video)
	queue = rl.GetVideoAudioQueueState(video)
	syncTune = rl.GetVideoAudioSyncTuning(video)
end while
	rl.PauseVideoStream video

if diag == null then
	print "Seek audio align smoke failed: timing diagnostics returned null"
	rl.UnloadVideoStream video
	exit 1
end if
if queue == null then
	print "Seek audio align smoke failed: queue state returned null"
	rl.UnloadVideoStream video
	exit 1
end if
if syncTune == null then
	print "Seek audio align smoke failed: sync tuning returned null"
	rl.UnloadVideoStream video
	exit 1
end if

if diag.syncMode != "audio-stream" and diag.syncMode != "wall-clock" then
	print "Seek audio align smoke failed: unexpected syncMode=" + diag.syncMode
	rl.UnloadVideoStream video
	exit 1
end if
if queue.audioClockSec == null or queue.audioLedTargetSec == null or queue.audioSyncOffsetSec == null then
	print "Seek audio align smoke failed: queue missing audio timing fields"
	rl.UnloadVideoStream video
	exit 1
end if
if queue.audioClockSec < seekPos - 0.5 then
	print "Seek audio align smoke failed: audioClockSec did not realign after seek"
	rl.UnloadVideoStream video
	exit 1
end if
if abs(queue.audioLedTargetSec - (queue.audioClockSec + queue.audioSyncOffsetSec)) > 0.3 then
	print "Seek audio align smoke failed: audioLedTargetSec and audio clock offset diverged after seek"
	rl.UnloadVideoStream video
	exit 1
end if
if abs(diag.videoAudioSyncSkewMs) > 600 then
	print "Seek audio align smoke failed: skew remained too large after seek: " + str(diag.videoAudioSyncSkewMs)
	rl.UnloadVideoStream video
	exit 1
end if
if queue.availableFrames <= 0 then
	print "Seek audio align smoke failed: expected buffered audio after seek"
	rl.UnloadVideoStream video
	exit 1
end if

print "seek audio align: seekPos=" + str(seekPos) + " audioClockSec=" + str(queue.audioClockSec) + " audioLedTargetSec=" + str(queue.audioLedTargetSec) + " skewMs=" + str(diag.videoAudioSyncSkewMs)
print "Seek audio align smoke done"
rl.UnloadVideoStream video
