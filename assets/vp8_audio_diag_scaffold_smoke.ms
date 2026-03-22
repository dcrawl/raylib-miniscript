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
decodeStub = null
decodeState = null
decodeBatch = null
resetState = null
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
if step.vorbisHeaderParseAttempted == null or step.vorbisHeadersReady == null then
	print "Audio diag/scaffold smoke failed: missing vorbis readiness fields"
	rl.UnloadVideoStream video
	exit 1
end if
if step.vorbisIdentificationSeen == null or step.vorbisCommentSeen == null or step.vorbisSetupSeen == null then
	print "Audio diag/scaffold smoke failed: missing vorbis header flags"
	rl.UnloadVideoStream video
	exit 1
end if
if step.vorbisHeaderSource == null or step.vorbisMissingHeaders == null or step.readyForDecode == null then
	print "Audio diag/scaffold smoke failed: missing decode readiness summary fields"
	rl.UnloadVideoStream video
	exit 1
end if

if meta.hasAudio and meta.audioCodec == "A_VORBIS" then
	if step.supported != 1 then
		print "Audio diag/scaffold smoke failed: expected scaffold support for A_VORBIS"
		rl.UnloadVideoStream video
		exit 1
	end if
	if step.vorbisHeaderParseAttempted != 1 then
		print "Audio diag/scaffold smoke failed: expected Vorbis header parse attempt"
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
	if step.readyForDecode != 1 then
		print "Audio diag/scaffold smoke failed: expected readyForDecode for A_VORBIS"
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeStub = rl.DecodeVideoAudioPacket(video)
	if decodeStub == null then
		print "Audio diag/scaffold smoke failed: decode stub result is null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.status == null or decodeStub.message == null or decodeStub.consumedPacket == null then
		print "Audio diag/scaffold smoke failed: decode stub missing structured fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.decodeSessionId == null then
		print "Audio diag/scaffold smoke failed: decode stub missing decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.readyForDecode != 1 then
		print "Audio diag/scaffold smoke failed: decode stub expected readyForDecode=1"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.consumedPacket != 1 then
		print "Audio diag/scaffold smoke failed: decode stub expected consumed packet"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.status != "decode-not-wired" then
		print "Audio diag/scaffold smoke failed: decode stub expected decode-not-wired status"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.packetBytes <= 0 then
		print "Audio diag/scaffold smoke failed: decode stub expected positive packetBytes"
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeState = rl.GetVideoAudioDecodeState(video)
	if decodeState == null then
		print "Audio diag/scaffold smoke failed: decode state result is null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.status == null or decodeState.totalReadPackets == null or decodeState.remainingPackets == null then
		print "Audio diag/scaffold smoke failed: decode state missing structured fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.decodeSessionId == null then
		print "Audio diag/scaffold smoke failed: decode state missing decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.readyForDecode != 1 then
		print "Audio diag/scaffold smoke failed: decode state expected readyForDecode=1"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.decodeSessionId != decodeStub.decodeSessionId then
		print "Audio diag/scaffold smoke failed: decode state/session id mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.totalReadPackets != decodeStub.totalReadPackets then
		print "Audio diag/scaffold smoke failed: decode state/read packet counter mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.status != "ready" and decodeState.status != "end-of-stream" then
		print "Audio diag/scaffold smoke failed: decode state unexpected status=" + decodeState.status
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeBatch = rl.DecodeVideoAudioPacketBatch(video, 2)
	if decodeBatch == null then
		print "Audio diag/scaffold smoke failed: decode batch result is null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if len(decodeBatch) < 1 then
		print "Audio diag/scaffold smoke failed: decode batch expected at least one item"
		rl.UnloadVideoStream video
		exit 1
	end if
	if len(decodeBatch) > 2 then
		print "Audio diag/scaffold smoke failed: decode batch exceeded maxPackets"
		rl.UnloadVideoStream video
		exit 1
	end if
	firstItem = decodeBatch[0]
	if firstItem.status == null or firstItem.message == null or firstItem.consumedPacket == null then
		print "Audio diag/scaffold smoke failed: decode batch item missing structured fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if firstItem.decodeSessionId == null then
		print "Audio diag/scaffold smoke failed: decode batch item missing decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if firstItem.decodeSessionId != decodeStub.decodeSessionId then
		print "Audio diag/scaffold smoke failed: decode batch/session id mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if firstItem.status != "decode-not-wired" then
		print "Audio diag/scaffold smoke failed: decode batch first item expected decode-not-wired"
		rl.UnloadVideoStream video
		exit 1
	end if
	if firstItem.consumedPacket != 1 then
		print "Audio diag/scaffold smoke failed: decode batch first item expected consumed packet"
		rl.UnloadVideoStream video
		exit 1
	end if
	if firstItem.packetBytes <= 0 then
		print "Audio diag/scaffold smoke failed: decode batch first item expected positive packetBytes"
		rl.UnloadVideoStream video
		exit 1
	end if

	resetState = rl.ResetVideoAudioDecodeSession(video)
	if resetState == null then
		print "Audio diag/scaffold smoke failed: reset decode session result is null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if resetState.totalReadPackets != 0 or resetState.nextPacketIndex != 0 then
		print "Audio diag/scaffold smoke failed: reset did not clear decode counters"
		rl.UnloadVideoStream video
		exit 1
	end if
	if resetState.decodeSessionId == null then
		print "Audio diag/scaffold smoke failed: reset state missing decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if resetState.decodeSessionId <= decodeStub.decodeSessionId then
		print "Audio diag/scaffold smoke failed: reset did not advance decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if resetState.readyForDecode != 1 then
		print "Audio diag/scaffold smoke failed: reset expected readyForDecode=1 when keeping seeded headers"
		rl.UnloadVideoStream video
		exit 1
	end if
end if

print "audio diag: packets=" + str(diag.packetCount) + " avgDelta=" + str(diag.avgDelta) + " nonMonotonic=" + str(diag.nonMonotonicCount)
print "audio scaffold: supported=" + str(step.supported) + " readCount=" + str(step.readCount) + " totalReadPackets=" + str(step.totalReadPackets) + " totalReadBytes=" + str(step.totalReadBytes)
print "audio headers: attempted=" + str(step.vorbisHeaderParseAttempted) + " ready=" + str(step.vorbisHeadersReady) + " id=" + str(step.vorbisIdentificationSeen) + " comment=" + str(step.vorbisCommentSeen) + " setup=" + str(step.vorbisSetupSeen)
print "audio readiness: source=" + step.vorbisHeaderSource + " missing=" + step.vorbisMissingHeaders + " readyForDecode=" + str(step.readyForDecode)
if decodeStub != null then
	print "audio decode stub: sessionId=" + str(decodeStub.decodeSessionId) + " status=" + decodeStub.status + " consumed=" + str(decodeStub.consumedPacket) + " packetIndex=" + str(decodeStub.packetIndex) + " packetBytes=" + str(decodeStub.packetBytes)
end if
if decodeState != null then
	print "audio decode state: sessionId=" + str(decodeState.decodeSessionId) + " status=" + decodeState.status + " totalReadPackets=" + str(decodeState.totalReadPackets) + " remaining=" + str(decodeState.remainingPackets) + " readyForDecode=" + str(decodeState.readyForDecode)
end if
if decodeBatch != null then
	print "audio decode batch: count=" + str(len(decodeBatch)) + " firstSessionId=" + str(decodeBatch[0].decodeSessionId) + " firstStatus=" + decodeBatch[0].status + " firstConsumed=" + str(decodeBatch[0].consumedPacket)
end if
if resetState != null then
	print "audio decode reset: sessionId=" + str(resetState.decodeSessionId) + " status=" + resetState.status + " totalReadPackets=" + str(resetState.totalReadPackets) + " nextPacketIndex=" + str(resetState.nextPacketIndex) + " readyForDecode=" + str(resetState.readyForDecode)
end if

rl.UnloadVideoStream video
print "Audio diag/scaffold smoke done"
