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
statusCodes = rl.GetVideoAudioDecodeStatuses()
caps = rl.GetVideoAudioDecodeCapabilities(video)

if statusCodes == null then
	print "Audio diag/scaffold smoke failed: decode status constants map is null"
	rl.UnloadVideoStream video
	exit 1
end if
if statusCodes.decodeNotWired == null or statusCodes.sessionMismatch == null or statusCodes.ok == null then
	print "Audio diag/scaffold smoke failed: decode status constants missing keys"
	rl.UnloadVideoStream video
	exit 1
end if

if caps == null then
	print "Audio diag/scaffold smoke failed: decode capability map is null"
	rl.UnloadVideoStream video
	exit 1
end if
if caps.backend == null or caps.decoderWired == null or caps.playbackWired == null or caps.avSyncWired == null or caps.decodedPcmFramesAvailable == null then
	print "Audio diag/scaffold smoke failed: decode capability map missing key fields"
	rl.UnloadVideoStream video
	exit 1
end if
if caps.decoderWired != 1 or caps.playbackWired != 1 or caps.avSyncWired != 1 then
	print "Audio diag/scaffold smoke failed: expected wired decoder with wired playback/sync capabilities"
	rl.UnloadVideoStream video
	exit 1
end if
if caps.status != statusCodes.ok then
	print "Audio diag/scaffold smoke failed: decode capability expected status=ok"
	rl.UnloadVideoStream video
	exit 1
end if
if caps.statusConstantsSupported != 1 or caps.sessionGuardSupported != 1 or caps.batchDecodeSupported != 1 then
	print "Audio diag/scaffold smoke failed: decode capability support flags missing"
	rl.UnloadVideoStream video
	exit 1
end if

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
staleDecode = null
staleBatch = null
staleStep = null
sessionStart = null
readyCheck = null
staleReadyCheck = null
staleDecodeState = null
capsAfterDecode = null
decodeConsume = null
queueBefore = null
queueAfter = null
queueDrained = null
syncTuneBefore = null
syncTuneAfter = null
driftStartConsumed = 0
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
if step.status == null or step.message == null then
	print "Audio diag/scaffold smoke failed: missing scaffold status fields"
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
	if step.status != statusCodes.ok then
		print "Audio diag/scaffold smoke failed: expected scaffold status=ok"
		rl.UnloadVideoStream video
		exit 1
	end if

	sessionStart = rl.CreateVideoAudioDecodeSession(video)
	if sessionStart == null then
		print "Audio diag/scaffold smoke failed: decode session helper returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if sessionStart.decodeSessionId == null or sessionStart.status == null or sessionStart.readyForDecode == null then
		print "Audio diag/scaffold smoke failed: decode session helper missing fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if sessionStart.decodeSessionId != step.decodeSessionId then
		print "Audio diag/scaffold smoke failed: decode session id mismatch with scaffold"
		rl.UnloadVideoStream video
		exit 1
	end if
	if sessionStart.status != "ready" and sessionStart.status != "end-of-stream" then
		print "Audio diag/scaffold smoke failed: decode session helper unexpected status=" + sessionStart.status
		rl.UnloadVideoStream video
		exit 1
	end if

	readyCheck = rl.IsVideoAudioDecodeReady(video, sessionStart.decodeSessionId)
	if readyCheck == null then
		print "Audio diag/scaffold smoke failed: readiness helper returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if readyCheck.status == null or readyCheck.message == null or readyCheck.readyForDecode == null then
		print "Audio diag/scaffold smoke failed: readiness helper missing fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if readyCheck.isCurrentSession != 1 then
		print "Audio diag/scaffold smoke failed: readiness helper expected current session"
		rl.UnloadVideoStream video
		exit 1
	end if
	if readyCheck.expectedSessionId != sessionStart.decodeSessionId then
		print "Audio diag/scaffold smoke failed: readiness helper expectedSessionId did not echo input"
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeStub = rl.DecodeVideoAudioPacket(video, sessionStart.decodeSessionId)
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
	if decodeStub.status != statusCodes.ok and decodeStub.status != statusCodes.decodeNotWired then
		print "Audio diag/scaffold smoke failed: decode stub expected ok or decode-not-wired status (got: " + decodeStub.status + ")"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.packetBytes <= 0 then
		print "Audio diag/scaffold smoke failed: decode stub expected positive packetBytes"
		rl.UnloadVideoStream video
		exit 1
	end if

	capsAfterDecode = rl.GetVideoAudioDecodeCapabilities(video)
	if capsAfterDecode == null or capsAfterDecode.decodedPcmFramesAvailable == null then
		print "Audio diag/scaffold smoke failed: decode capability map missing post-decode metric"
		rl.UnloadVideoStream video
		exit 1
	end if
	if capsAfterDecode.decodedPcmFramesAvailable < caps.decodedPcmFramesAvailable then
		print "Audio diag/scaffold smoke failed: decodedPcmFramesAvailable regressed"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeStub.decodedSamples > 0 and capsAfterDecode.decodedPcmFramesAvailable < caps.decodedPcmFramesAvailable + decodeStub.decodedSamples then
		print "Audio diag/scaffold smoke failed: decodedPcmFramesAvailable did not advance with decodedSamples"
		rl.UnloadVideoStream video
		exit 1
	end if

	consumeRequest = decodeStub.decodedSamples
	if consumeRequest <= 0 then consumeRequest = 32
	decodeConsume = rl.ConsumeVideoDecodedPcmFrames(video, consumeRequest)
	if decodeConsume == null then
		print "Audio diag/scaffold smoke failed: decode consume helper returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeConsume.status == null or decodeConsume.message == null or decodeConsume.consumedFrames == null then
		print "Audio diag/scaffold smoke failed: decode consume helper missing fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeConsume.status != statusCodes.ok then
		print "Audio diag/scaffold smoke failed: decode consume helper expected status=ok"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeConsume.consumedFrames > consumeRequest then
		print "Audio diag/scaffold smoke failed: decode consume helper consumed more than requested"
		rl.UnloadVideoStream video
		exit 1
	end if

	topUp = rl.DecodeVideoAudioPacketBatch(video, 8, sessionStart.decodeSessionId)
	if topUp == null or len(topUp) < 1 then
		print "Audio diag/scaffold smoke failed: queue top-up decode batch returned no items"
		rl.UnloadVideoStream video
		exit 1
	end if

	// Validate GetVideoFrameTimingDiagnostics
	timingDiag = rl.GetVideoFrameTimingDiagnostics(video)
	if timingDiag == null then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.syncMode == null or timingDiag.totalFramesDecoded == null or timingDiag.totalFramesSkipped == null then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics missing required fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.totalFrameDropEvents == null or timingDiag.lastDecodeBudgetUsed == null or timingDiag.lastDecodeBudgetExhausted == null then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics missing budget fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.lastDecodedFramePts == null or timingDiag.lastUpdateTargetPts == null or timingDiag.videoAudioSyncSkewMs == null then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics missing pts/skew fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.audioLedSyncEnabled == null or timingDiag.audioSyncClampWindowMs == null or timingDiag.audioSyncOffsetSec == null or timingDiag.audioClockSec == null or timingDiag.audioLedTargetSec == null then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics missing audio-led sync fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.framesBuffered == null or timingDiag.framesBuffered < 0 then
		print "Audio diag/scaffold smoke failed: GetVideoFrameTimingDiagnostics invalid framesBuffered"
		rl.UnloadVideoStream video
		exit 1
	end if
	if timingDiag.syncMode != "wall-clock" and timingDiag.syncMode != "audio-stream" then
		print "Audio diag/scaffold smoke failed: expected syncMode in {wall-clock,audio-stream}, got " + timingDiag.syncMode
		rl.UnloadVideoStream video
		exit 1
	end if

	queueBefore = rl.GetVideoAudioQueueState(video)
	if queueBefore == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.availableFrames == null or queueBefore.consumedFrames == null or queueBefore.latencyMs == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.clockEstimatedFrames == null or queueBefore.clockConsumedFrames == null or queueBefore.clockDriftFrames == null or queueBefore.clockDriftMs == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing clock drift fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.autoRefillTriggerCount == null or queueBefore.autoRefillPacketsDecoded == null or queueBefore.autoRefillTargetHitCount == null or queueBefore.autoRefillLastLatencyMs == null or queueBefore.autoRefillTargetLatencyMs == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing auto-refill fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.refillPacketsPerTrigger == null or queueBefore.refillLatencyGainMs == null or queueBefore.refillTargetHitRate == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing refill-efficiency fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.refillTargetHitRate < 0 or queueBefore.refillTargetHitRate > 1 then
		print "Audio diag/scaffold smoke failed: refillTargetHitRate out of bounds"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.estimatedBufferingMarginMs == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing estimatedBufferingMarginMs field"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.tuningHint == null or queueBefore.suggestedAdjustmentMs == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing tuning hint fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.syncMode == null or queueBefore.audioLedSyncEnabled == null or queueBefore.audioSyncClampWindowMs == null or queueBefore.audioSyncOffsetSec == null or queueBefore.audioClockSec == null or queueBefore.audioLedTargetSec == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper missing audio sync tuning fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.clockEstimatedFrames < queueBefore.clockConsumedFrames then
		print "Audio diag/scaffold smoke failed: queue clock estimate regressed below consumed"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueBefore.status != statusCodes.ok then
		print "Audio diag/scaffold smoke failed: queue telemetry helper expected status=ok"
		rl.UnloadVideoStream video
		exit 1
	end if

	syncTuneBefore = rl.GetVideoAudioSyncTuning(video)
	if syncTuneBefore == null then
		print "Audio diag/scaffold smoke failed: GetVideoAudioSyncTuning returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if syncTuneBefore.audioLedSyncEnabled == null or syncTuneBefore.audioSyncClampWindowMs == null then
		print "Audio diag/scaffold smoke failed: GetVideoAudioSyncTuning missing fields"
		rl.UnloadVideoStream video
		exit 1
	end if

	tuneSet = rl.SetVideoAudioSyncTuning(video, 1, 35)
	if tuneSet == null or tuneSet.status != statusCodes.ok then
		print "Audio diag/scaffold smoke failed: SetVideoAudioSyncTuning failed"
		rl.UnloadVideoStream video
		exit 1
	end if
	syncTuneAfter = rl.GetVideoAudioSyncTuning(video)
	if syncTuneAfter == null or syncTuneAfter.audioLedSyncEnabled != 1 then
		print "Audio diag/scaffold smoke failed: audio-led sync should remain enabled"
		rl.UnloadVideoStream video
		exit 1
	end if
	if syncTuneAfter.audioSyncClampWindowMs != 35 then
		print "Audio diag/scaffold smoke failed: expected audioSyncClampWindowMs=35 after tuning"
		rl.UnloadVideoStream video
		exit 1
	end if

	drainAll = rl.ConsumeVideoDecodedPcmFrames(video, 0)
	if drainAll == null or drainAll.status != statusCodes.ok then
		print "Audio diag/scaffold smoke failed: queue drain-all helper failed"
		rl.UnloadVideoStream video
		exit 1
	end if
	queueDrained = rl.GetVideoAudioQueueState(video)
	if queueDrained == null then
		print "Audio diag/scaffold smoke failed: queue telemetry after drain returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueDrained.availableFrames != 0 then
		print "Audio diag/scaffold smoke failed: expected queue drained to zero available frames"
		rl.UnloadVideoStream video
		exit 1
	end if

	driftStartConsumed = queueDrained.consumedFrames
	rl.SetVideoPlaybackRate video, 4
	rl.PlayVideoStream video
	driftStartTime = rl.GetTime
	while rl.GetTime - driftStartTime < 0.2
		rl.UpdateVideoStream video
	end while
	rl.PauseVideoStream video
	rl.SetVideoPlaybackRate video, 1

	queueAfter = rl.GetVideoAudioQueueState(video)
	if queueAfter == null then
		print "Audio diag/scaffold smoke failed: queue telemetry helper post-drift returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.consumedFrames < driftStartConsumed then
		print "Audio diag/scaffold smoke failed: queue consumedFrames regressed after UpdateVideoStream"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.clockEstimatedFrames < queueAfter.clockConsumedFrames then
		print "Audio diag/scaffold smoke failed: queue clock estimate regressed below consumed after UpdateVideoStream"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.clockDriftFrames < 0 or queueAfter.clockDriftMs < 0 then
		print "Audio diag/scaffold smoke failed: queue clock drift became negative"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.autoRefillTriggerCount <= queueBefore.autoRefillTriggerCount then
		print "Audio diag/scaffold smoke failed: expected auto-refill trigger count to advance"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.autoRefillPacketsDecoded <= queueBefore.autoRefillPacketsDecoded then
		print "Audio diag/scaffold smoke failed: expected auto-refill decoded-packet counter to advance"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.refillPacketsPerTrigger <= 0 then
		print "Audio diag/scaffold smoke failed: expected positive refillPacketsPerTrigger"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.refillLatencyGainMs < 0 then
		print "Audio diag/scaffold smoke failed: refillLatencyGainMs became negative"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.refillTargetHitRate < 0 or queueAfter.refillTargetHitRate > 1 then
		print "Audio diag/scaffold smoke failed: refillTargetHitRate out of bounds after UpdateVideoStream"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.estimatedBufferingMarginMs == null then
		print "Audio diag/scaffold smoke failed: estimatedBufferingMarginMs missing after UpdateVideoStream"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.tuningHint == null or queueAfter.suggestedAdjustmentMs == null then
		print "Audio diag/scaffold smoke failed: tuning hint fields missing after UpdateVideoStream"
		rl.UnloadVideoStream video
		exit 1
	end if
	// Validate tuning hint values are reasonable
	if queueAfter.tuningHint != "insufficient-data" and queueAfter.tuningHint != "increase-target" and queueAfter.tuningHint != "optimal" and queueAfter.tuningHint != "decrease-target" then
		print "Audio diag/scaffold smoke failed: unexpected tuningHint value: " + queueAfter.tuningHint
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.totalDecodedFrames <= queueDrained.totalDecodedFrames then
		print "Audio diag/scaffold smoke failed: expected UpdateVideoStream auto-refill to decode packets"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.availableFrames <= 0 then
		print "Audio diag/scaffold smoke failed: expected non-zero queue availability after auto-refill"
		rl.UnloadVideoStream video
		exit 1
	end if
	if queueAfter.audioSyncClampWindowMs != 35 then
		print "Audio diag/scaffold smoke failed: queue telemetry did not reflect configured clamp window"
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeState = rl.GetVideoAudioDecodeState(video, sessionStart.decodeSessionId)
	if decodeState == null then
		print "Audio diag/scaffold smoke failed: decode state result is null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.status == null or decodeState.message == null or decodeState.totalReadPackets == null or decodeState.remainingPackets == null then
		print "Audio diag/scaffold smoke failed: decode state missing structured fields"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.decodeSessionId == null then
		print "Audio diag/scaffold smoke failed: decode state missing decodeSessionId"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.expectedSessionId != sessionStart.decodeSessionId then
		print "Audio diag/scaffold smoke failed: decode state expectedSessionId did not echo input"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.isCurrentSession != 1 then
		print "Audio diag/scaffold smoke failed: decode state expected current session"
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
	if decodeState.totalReadPackets < decodeStub.totalReadPackets then
		print "Audio diag/scaffold smoke failed: decode state/read packet counter regressed"
		rl.UnloadVideoStream video
		exit 1
	end if
	if decodeState.status != "ready" and decodeState.status != "end-of-stream" then
		print "Audio diag/scaffold smoke failed: decode state unexpected status=" + decodeState.status
		rl.UnloadVideoStream video
		exit 1
	end if

	decodeBatch = rl.DecodeVideoAudioPacketBatch(video, 2, sessionStart.decodeSessionId)
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
	if firstItem.status != statusCodes.ok and firstItem.status != statusCodes.decodeNotWired then
		print "Audio diag/scaffold smoke failed: decode batch first item expected ok or decode-not-wired (got: " + firstItem.status + ")"
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
	if resetState.message == null then
		print "Audio diag/scaffold smoke failed: reset state missing message"
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

	staleDecode = rl.DecodeVideoAudioPacket(video, decodeStub.decodeSessionId)
	if staleDecode == null then
		print "Audio diag/scaffold smoke failed: stale decode session guard returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecode.status != statusCodes.sessionMismatch then
		print "Audio diag/scaffold smoke failed: stale decode expected session-mismatch status"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecode.consumedPacket != 0 then
		print "Audio diag/scaffold smoke failed: stale decode should not consume packet"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecode.decodeSessionId != resetState.decodeSessionId then
		print "Audio diag/scaffold smoke failed: stale decode should report current session id"
		rl.UnloadVideoStream video
		exit 1
	end if

	staleBatch = rl.DecodeVideoAudioPacketBatch(video, 2, decodeStub.decodeSessionId)
	if staleBatch == null or len(staleBatch) < 1 then
		print "Audio diag/scaffold smoke failed: stale batch guard expected one result"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleBatch[0].status != statusCodes.sessionMismatch then
		print "Audio diag/scaffold smoke failed: stale batch expected session-mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleBatch[0].consumedPacket != 0 then
		print "Audio diag/scaffold smoke failed: stale batch should not consume packet"
		rl.UnloadVideoStream video
		exit 1
	end if

	staleStep = rl.StepVideoAudioDecodeScaffold(video, 2, decodeStub.decodeSessionId)
	if staleStep == null then
		print "Audio diag/scaffold smoke failed: stale step session guard returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleStep.status != statusCodes.sessionMismatch then
		print "Audio diag/scaffold smoke failed: stale step expected session-mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleStep.readCount != 0 then
		print "Audio diag/scaffold smoke failed: stale step should not read packets"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleStep.decodeSessionId != resetState.decodeSessionId then
		print "Audio diag/scaffold smoke failed: stale step should report current session id"
		rl.UnloadVideoStream video
		exit 1
	end if

	staleReadyCheck = rl.IsVideoAudioDecodeReady(video, decodeStub.decodeSessionId)
	if staleReadyCheck == null then
		print "Audio diag/scaffold smoke failed: stale readiness helper returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleReadyCheck.status != statusCodes.sessionMismatch then
		print "Audio diag/scaffold smoke failed: stale readiness helper expected session-mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleReadyCheck.isCurrentSession != 0 then
		print "Audio diag/scaffold smoke failed: stale readiness helper expected isCurrentSession=0"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleReadyCheck.expectedSessionId != decodeStub.decodeSessionId then
		print "Audio diag/scaffold smoke failed: stale readiness helper expectedSessionId did not echo input"
		rl.UnloadVideoStream video
		exit 1
	end if

	staleDecodeState = rl.GetVideoAudioDecodeState(video, decodeStub.decodeSessionId)
	if staleDecodeState == null then
		print "Audio diag/scaffold smoke failed: stale decode state returned null"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecodeState.status != statusCodes.sessionMismatch then
		print "Audio diag/scaffold smoke failed: stale decode state expected session-mismatch"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecodeState.isCurrentSession != 0 then
		print "Audio diag/scaffold smoke failed: stale decode state expected isCurrentSession=0"
		rl.UnloadVideoStream video
		exit 1
	end if
	if staleDecodeState.expectedSessionId != decodeStub.decodeSessionId then
		print "Audio diag/scaffold smoke failed: stale decode state expectedSessionId did not echo input"
		rl.UnloadVideoStream video
		exit 1
	end if
end if

print "audio diag: packets=" + str(diag.packetCount) + " avgDelta=" + str(diag.avgDelta) + " nonMonotonic=" + str(diag.nonMonotonicCount)
print "audio status constants: ok=" + statusCodes.ok + " decodeNotWired=" + statusCodes.decodeNotWired + " sessionMismatch=" + statusCodes.sessionMismatch
print "audio decode capabilities: backend=" + caps.backend + " decoderWired=" + str(caps.decoderWired) + " playbackWired=" + str(caps.playbackWired) + " avSyncWired=" + str(caps.avSyncWired) + " decodedPcmFramesAvailable=" + str(caps.decodedPcmFramesAvailable)
print "audio scaffold: supported=" + str(step.supported) + " readCount=" + str(step.readCount) + " totalReadPackets=" + str(step.totalReadPackets) + " totalReadBytes=" + str(step.totalReadBytes)
print "audio headers: attempted=" + str(step.vorbisHeaderParseAttempted) + " ready=" + str(step.vorbisHeadersReady) + " id=" + str(step.vorbisIdentificationSeen) + " comment=" + str(step.vorbisCommentSeen) + " setup=" + str(step.vorbisSetupSeen)
print "audio readiness: source=" + step.vorbisHeaderSource + " missing=" + step.vorbisMissingHeaders + " readyForDecode=" + str(step.readyForDecode)
if decodeStub != null then
	print "audio decode stub: sessionId=" + str(decodeStub.decodeSessionId) + " status=" + decodeStub.status + " consumed=" + str(decodeStub.consumedPacket) + " packetIndex=" + str(decodeStub.packetIndex) + " packetBytes=" + str(decodeStub.packetBytes) + " decodedSamples=" + str(decodeStub.decodedSamples)
end if
if capsAfterDecode != null then
	print "audio decode capabilities post-decode: decodedPcmFramesAvailable=" + str(capsAfterDecode.decodedPcmFramesAvailable)
end if
if decodeConsume != null then
	print "audio decode consume: consumedFrames=" + str(decodeConsume.consumedFrames) + " available=" + str(decodeConsume.decodedPcmFramesAvailable) + " consumedTotal=" + str(decodeConsume.decodedPcmFramesConsumed) + " status=" + decodeConsume.status
end if
if timingDiag != null then
	print "video timing: syncMode=" + timingDiag.syncMode + " totalDecoded=" + str(timingDiag.totalFramesDecoded) + " totalSkipped=" + str(timingDiag.totalFramesSkipped) + " dropEvents=" + str(timingDiag.totalFrameDropEvents) + " lastBudgetUsed=" + str(timingDiag.lastDecodeBudgetUsed) + " budgetExhausted=" + str(timingDiag.lastDecodeBudgetExhausted) + " lastFramePts=" + str(timingDiag.lastDecodedFramePts) + " targetPts=" + str(timingDiag.lastUpdateTargetPts) + " audioSyncSkewMs=" + str(timingDiag.videoAudioSyncSkewMs) + " framesBuffered=" + str(timingDiag.framesBuffered)
end if
if queueBefore != null then
	print "audio queue state: available=" + str(queueBefore.availableFrames) + " consumed=" + str(queueBefore.consumedFrames) + " latencyMs=" + str(queueBefore.latencyMs) + " bufferingMarginMs=" + str(queueBefore.estimatedBufferingMarginMs) + " clockEstimated=" + str(queueBefore.clockEstimatedFrames) + " clockConsumed=" + str(queueBefore.clockConsumedFrames) + " clockDriftFrames=" + str(queueBefore.clockDriftFrames) + " clockDriftMs=" + str(queueBefore.clockDriftMs) + " autoRefillTriggers=" + str(queueBefore.autoRefillTriggerCount) + " autoRefillPackets=" + str(queueBefore.autoRefillPacketsDecoded) + " autoRefillHits=" + str(queueBefore.autoRefillTargetHitCount) + " autoRefillLastLatencyMs=" + str(queueBefore.autoRefillLastLatencyMs) + " autoRefillTargetLatencyMs=" + str(queueBefore.autoRefillTargetLatencyMs) + " refillPacketsPerTrigger=" + str(queueBefore.refillPacketsPerTrigger) + " refillLatencyGainMs=" + str(queueBefore.refillLatencyGainMs) + " refillTargetHitRate=" + str(queueBefore.refillTargetHitRate) + " tuningHint=" + queueBefore.tuningHint + " suggestedAdj=" + str(queueBefore.suggestedAdjustmentMs) + " status=" + queueBefore.status
end if
if queueDrained != null then
	print "audio queue drained: available=" + str(queueDrained.availableFrames) + " consumed=" + str(queueDrained.consumedFrames) + " totalDecoded=" + str(queueDrained.totalDecodedFrames)
end if
if queueAfter != null then
	print "audio queue drift update: beforeConsumed=" + str(driftStartConsumed) + " afterConsumed=" + str(queueAfter.consumedFrames) + " afterAvailable=" + str(queueAfter.availableFrames) + " afterTotalDecoded=" + str(queueAfter.totalDecodedFrames) + " latencyMs=" + str(queueAfter.latencyMs) + " bufferingMarginMs=" + str(queueAfter.estimatedBufferingMarginMs) + " clockEstimated=" + str(queueAfter.clockEstimatedFrames) + " clockConsumed=" + str(queueAfter.clockConsumedFrames) + " clockDriftFrames=" + str(queueAfter.clockDriftFrames) + " clockDriftMs=" + str(queueAfter.clockDriftMs) + " autoRefillTriggers=" + str(queueAfter.autoRefillTriggerCount) + " autoRefillPackets=" + str(queueAfter.autoRefillPacketsDecoded) + " autoRefillHits=" + str(queueAfter.autoRefillTargetHitCount) + " autoRefillLastLatencyMs=" + str(queueAfter.autoRefillLastLatencyMs) + " autoRefillTargetLatencyMs=" + str(queueAfter.autoRefillTargetLatencyMs) + " refillPacketsPerTrigger=" + str(queueAfter.refillPacketsPerTrigger) + " refillLatencyGainMs=" + str(queueAfter.refillLatencyGainMs) + " refillTargetHitRate=" + str(queueAfter.refillTargetHitRate) + " tuningHint=" + queueAfter.tuningHint + " suggestedAdj=" + str(queueAfter.suggestedAdjustmentMs)
end if
if decodeState != null then
	print "audio decode state: sessionId=" + str(decodeState.decodeSessionId) + " status=" + decodeState.status + " msg=" + decodeState.message + " totalReadPackets=" + str(decodeState.totalReadPackets) + " remaining=" + str(decodeState.remainingPackets) + " readyForDecode=" + str(decodeState.readyForDecode)
	print "audio decode state expectedSessionId echo: expected=" + str(decodeState.expectedSessionId) + " input=" + str(sessionStart.decodeSessionId)
end if
if decodeBatch != null then
	print "audio decode batch: count=" + str(len(decodeBatch)) + " firstSessionId=" + str(decodeBatch[0].decodeSessionId) + " firstStatus=" + decodeBatch[0].status + " firstConsumed=" + str(decodeBatch[0].consumedPacket)
end if
if resetState != null then
	print "audio decode reset: sessionId=" + str(resetState.decodeSessionId) + " status=" + resetState.status + " msg=" + resetState.message + " totalReadPackets=" + str(resetState.totalReadPackets) + " nextPacketIndex=" + str(resetState.nextPacketIndex) + " readyForDecode=" + str(resetState.readyForDecode)
end if
if staleDecode != null then
	print "audio decode stale guard: status=" + staleDecode.status + " consumed=" + str(staleDecode.consumedPacket) + " sessionId=" + str(staleDecode.decodeSessionId)
end if
if staleBatch != null then
	print "audio decode stale batch guard: status=" + staleBatch[0].status + " consumed=" + str(staleBatch[0].consumedPacket) + " sessionId=" + str(staleBatch[0].decodeSessionId)
end if
if staleStep != null then
	print "audio decode stale step guard: status=" + staleStep.status + " readCount=" + str(staleStep.readCount) + " sessionId=" + str(staleStep.decodeSessionId)
end if
if sessionStart != null then
	print "audio decode session start: sessionId=" + str(sessionStart.decodeSessionId) + " status=" + sessionStart.status + " readyForDecode=" + str(sessionStart.readyForDecode)
end if
if readyCheck != null then
	print "audio decode ready check: sessionId=" + str(readyCheck.decodeSessionId) + " status=" + readyCheck.status + " readyForDecode=" + str(readyCheck.readyForDecode) + " isCurrent=" + str(readyCheck.isCurrentSession)
	print "audio decode ready expectedSessionId echo: expected=" + str(readyCheck.expectedSessionId) + " input=" + str(sessionStart.decodeSessionId)
end if
if staleReadyCheck != null then
	print "audio decode stale ready check: sessionId=" + str(staleReadyCheck.decodeSessionId) + " status=" + staleReadyCheck.status + " isCurrent=" + str(staleReadyCheck.isCurrentSession)
	print "audio decode stale ready expectedSessionId echo: expected=" + str(staleReadyCheck.expectedSessionId) + " input=" + str(decodeStub.decodeSessionId)
end if
if staleDecodeState != null then
	print "audio decode stale state check: sessionId=" + str(staleDecodeState.decodeSessionId) + " status=" + staleDecodeState.status + " isCurrent=" + str(staleDecodeState.isCurrentSession)
	print "audio decode stale state expectedSessionId echo: expected=" + str(staleDecodeState.expectedSessionId) + " input=" + str(decodeStub.decodeSessionId)
end if

rl.UnloadVideoStream video
print "Audio diag/scaffold smoke done"
