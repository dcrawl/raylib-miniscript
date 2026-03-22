// Audio-metadata smoke test for GetVideoInfo/GetVideoMetadata.
// Verifies new audio fields exist and can be read from MiniScript.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	video = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if video == null then
	print "Audio meta smoke failed: expected valid video load"
	print "audio meta error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
meta2 = rl.GetVideoMetadata(video)

if meta.hasAudio == null then
	print "Audio meta smoke failed: missing hasAudio field"
	rl.UnloadVideoStream video
	exit 1
end if
if meta.audioCodec == null then
	print "Audio meta smoke failed: missing audioCodec field"
	rl.UnloadVideoStream video
	exit 1
end if
if meta.audioSampleRate == null then
	print "Audio meta smoke failed: missing audioSampleRate field"
	rl.UnloadVideoStream video
	exit 1
end if
if meta.audioChannels == null then
	print "Audio meta smoke failed: missing audioChannels field"
	rl.UnloadVideoStream video
	exit 1
end if

if meta2.hasAudio == null then
	print "Audio meta smoke failed: metadata alias missing hasAudio field"
	rl.UnloadVideoStream video
	exit 1
end if

print "audio meta: hasAudio=" + str(meta.hasAudio) + " codec=" + meta.audioCodec + " sampleRate=" + str(meta.audioSampleRate) + " channels=" + str(meta.audioChannels)

rl.UnloadVideoStream video
print "Audio meta smoke done"
