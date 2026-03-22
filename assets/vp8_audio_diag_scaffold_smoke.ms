// Audio diagnostics + decode-scaffold smoke test.
// Verifies packet delta diagnostics and desktop packet-reader scaffold behavior.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	print "Audio diag/scaffold smoke failed: expected valid WebM load"
	print "audio diag/scaffold error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
diag = rl.GetVideoAudioIndexDiagnostics(video)

if diag == null then
	print "Audio diag/scaffold smoke failed: diagnostics map is null"
	rl.UnloadVideoStream video
	exit 1
end if
if diag.packetCount == null or diag.avgDelta == null or diag.nonMonotonicCount == null then
	print "Audio diag/scaffold smoke failed: missing diagnostics fields"
	rl.UnloadVideoStream video
	exit 1
end if
if diag.packetCount != meta.audioPacketCount then
	print "Audio diag/scaffold smoke failed: packetCount mismatch"
	rl.UnloadVideoStream video
	exit 1
end if
if diag.packetCount > 1 and diag.avgDelta <= 0 then
	print "Audio diag/scaffold smoke failed: expected positive avgDelta"
	rl.UnloadVideoStream video
	exit 1
end if

step = rl.StepVideoAudioDecodeScaffold(video, 4)
if step == null then
	print "Audio diag/scaffold smoke failed: scaffold result is null"
	rl.UnloadVideoStream video
	exit 1
end if
if step.supported == null or step.readCount == null or step.totalReadPackets == null then
	print "Audio diag/scaffold smoke failed: missing scaffold fields"
	rl.UnloadVideoStream video
	exit 1
end if

if meta.hasAudio and meta.audioCodec == "A_VORBIS" then
	if step.supported != 1 then
		print "Audio diag/scaffold smoke failed: expected scaffold support for A_VORBIS"
		rl.UnloadVideoStream video
		exit 1
	end if
	if step.readCount <= 0 then
		print "Audio diag/scaffold smoke failed: expected packet reads"
		rl.UnloadVideoStream video
		exit 1
	end if
	if step.totalReadBytes <= 0 then
		print "Audio diag/scaffold smoke failed: expected positive read bytes"
		rl.UnloadVideoStream video
		exit 1
	end if
end if

print "audio diag: packets=" + str(diag.packetCount) + " avgDelta=" + str(diag.avgDelta) + " nonMonotonic=" + str(diag.nonMonotonicCount)
print "audio scaffold: supported=" + str(step.supported) + " readCount=" + str(step.readCount) + " totalReadPackets=" + str(step.totalReadPackets) + " totalReadBytes=" + str(step.totalReadBytes)

rl.UnloadVideoStream video
print "Audio diag/scaffold smoke done"
