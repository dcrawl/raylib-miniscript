// Audio packet-index smoke test for WebM parser metadata.
// Verifies audioPacketCount and packet timestamp range fields.

rl = raylib
video = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if video == null then
	print "Audio packet index smoke failed: expected valid WebM load"
	print "audio packet index error=" + rl.GetVideoLastError()
	exit 1
end if

meta = rl.GetVideoInfo(video)
meta2 = rl.GetVideoMetadata(video)

if meta.audioPacketCount == null then
	print "Audio packet index smoke failed: missing audioPacketCount"
	rl.UnloadVideoStream video
	exit 1
end if
if meta.audioFirstPacketTime == null then
	print "Audio packet index smoke failed: missing audioFirstPacketTime"
	rl.UnloadVideoStream video
	exit 1
end if
if meta.audioLastPacketTime == null then
	print "Audio packet index smoke failed: missing audioLastPacketTime"
	rl.UnloadVideoStream video
	exit 1
end if
if meta2.audioPacketCount == null then
	print "Audio packet index smoke failed: metadata alias missing audioPacketCount"
	rl.UnloadVideoStream video
	exit 1
end if

if meta.hasAudio then
	if meta.audioPacketCount <= 0 then
		print "Audio packet index smoke failed: expected positive packet count"
		rl.UnloadVideoStream video
		exit 1
	end if
	if meta.audioFirstPacketTime > meta.audioLastPacketTime then
		print "Audio packet index smoke failed: invalid packet timestamp range"
		rl.UnloadVideoStream video
		exit 1
	end if
	if meta.audioFirstPacketTime < 0 then
		print "Audio packet index smoke failed: first packet time negative"
		rl.UnloadVideoStream video
		exit 1
	end if
	if meta.audioLastPacketTime > (meta.timeLength + 0.5) then
		print "Audio packet index smoke failed: last packet exceeds video duration"
		rl.UnloadVideoStream video
		exit 1
	end if
else
	if meta.audioPacketCount != 0 then
		print "Audio packet index smoke failed: expected zero packet count when hasAudio=0"
		rl.UnloadVideoStream video
		exit 1
	end if
end if

print "audio packets: count=" + str(meta.audioPacketCount) + " first=" + str(meta.audioFirstPacketTime) + " last=" + str(meta.audioLastPacketTime) + " hasAudio=" + str(meta.hasAudio)

rl.UnloadVideoStream video
print "Audio packet index smoke done"
