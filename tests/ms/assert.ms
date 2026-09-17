// Minimal assert-based test helper for headless MiniScript test scripts.
// Import with `import "assert"`, call assertTrue/assertEqual per check,
// then reportAndExit at the end of the script -- exits 0 if everything
// passed, 1 (via the `exit` intrinsic) otherwise, so a shell driver can
// read pass/fail off the process exit code.

checkCount = 0
failCount = 0

assertTrue = function(cond, msg)
	outer.checkCount = checkCount + 1
	if cond then
		print "ok   " + msg
	else
		outer.failCount = failCount + 1
		print "FAIL " + msg
	end if
end function

assertEqual = function(actual, expected, msg)
	assertTrue(actual == expected, msg + " (got " + actual + ", expected " + expected + ")")
end function

reportAndExit = function
	print ""
	print checkCount + " checks, " + failCount + " failures"
	if failCount > 0 then
		exit(1)
	else
		exit(0)
	end if
end function
