// Error-diagnostics smoke test for GetVideoLastError.
// Verifies load failure surfaces a readable message and valid load still works.

rl = raylib

bad = rl.LoadVideoStream("assets/does_not_exist.webm")
if bad != null then
	print "Error smoke failed: expected missing file load to fail"
	exit 1
end if

loadErr = rl.GetVideoLastError()
print "error smoke loadErr=" + loadErr
if loadErr == "" then
	print "Error smoke failed: expected non-empty load error"
	exit 1
end if

good = rl.LoadVideoStream("assets/EmberfallMovie.webm")
if good == null then
	good = rl.LoadVideoStream("assets/EmberfallMovie.ivf")
end if
if good == null then
	print "Error smoke failed: expected valid video load"
	print "error smoke secondLoadErr=" + rl.GetVideoLastError()
	exit 1
end if

playerErr = rl.GetVideoLastError(good)
print "error smoke playerErr=" + playerErr

rl.UnloadVideoStream good
print "Error smoke done"
