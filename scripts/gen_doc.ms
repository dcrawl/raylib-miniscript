#!/usr/bin/env miniscript

// This script generates API_DOC.md by crawling the source files.

import "stringUtil"


magicValues = {
	"Value::null": "null",
	"Value::zero": "0",
	"Value::one": "1",
	"QuaternionToValue(QuaternionIdentity())": "QuaternionIdentity",
	"MatrixToValue(MatrixIdentity())": "MatrixIdentity" }

valToText = function(cppVal)
	if magicValues.hasIndex(cppVal) then return magicValues[cppVal]
	m = cppVal.match("Value({val})")
	if m then
		if (m.val.startsWith("Vector2{") or m.val.startsWith("Vector3{") or 
		  m.val.startsWith("Vector4{")) and m.val[-1] == "}" then
			return "[" + m.val[8:-1] + "]"
		end if
		return m.val
	end if
	return cppVal
end function

isValidType = function(type)
	if not type then return false
	if not ("A" <= type[0] <= "Z") and not ("a" <= type[0] <= "z") then return false
	if type.contains(" ") then return false
	return true
end function

functionNotes = {}  // key: API name; value: notes from Raylib source

// Process the given .c file (Raylib source), finding function notes and 
// stuffing them into the global functionNotes map.
processRaylibFile = function(path)
	print "Scanning: " + path
	found = 0
	notes = []
	for line in file.readLines(path)
		if line.startsWith("// ") then
			notes.push line[3:]
			continue
		end if
		if line == "" or line.startsWith("//---") then
			notes = []
		end if
		if line.startsWith("RMAPI ") then line = line[6:]
		m = ("^" + line + "$").match("^{type} {name}({params})$")
		if m then
			if not isValidType(m.type) or m.name.contains(" ") then
				notes = []
				continue
			end if
			if not notes then continue
			if m.name[0] == "*" then m.name = m.name[1:]
			functionNotes[m.name] = notes.join(" ")
			//print " - " + m.name + ": " + functionNotes[m.name]
			notes = []
			found += 1
		end if			
	end for
	print "   " + found + " APIs"
end function

// Process the given .cpp file (MiniScript intrinsics binding Raylib functions),
// adding a table to the output for all the APIs that we found.
processBindingFile = function(path)
	print "Processing: " + path
	apiCount = 0
	
	params = []
	for line in file.readLines(path)
		line = line.trim
		
		if line.startsWith("i = Intrinsic::Create(") then
			params = []
			continue
		end if
		
		m = line.match("i->AddParam(""{param}"", {defval});")
		if m then
			params.push "**" + m.param + "**=" + valToText(m.defval)
			continue
		end if

		m = line.match("i->AddParam(""{param}"");")
		if m then
			params.push "**" + m.param + "**"
			continue
		end if

		m = line.match("raylibModule.SetValue(""{api}"", i->GetFunc());")
		if m then
			if apiCount == 0 then
				outLines.push ""
				outLines.push "## " + (file.name(path) - ".cpp")
				outLines.push ""
				outLines.push "|Name | Parameters | Purpose |"
				outLines.push "|-----|------------|---------|"
			end if
			apiCount += 1
		
			if functionNotes.hasIndex(m.api) then
				note = functionNotes[m.api]
			else
				print "   NO NOTE FOUND FOR: " + m.api
				note = ""
			end if
			outLines.push "|" + m.api + " |" + params.join(", ") + " |" + note + " |"
			params = []
		end if
	end for
	print "   " + apiCount + " APIs"
end function


// Begin by verifying that we can access the files we need.
raylibSrc = file.child("raylib", "src")
raylibSrcInfo = file.info(raylibSrc)
if not raylibSrcInfo or not raylibSrcInfo.isDirectory then
	print "Unable to read " + raylibSrc
	exit
end if

bindingSrc = "src"
bindingSrcInfo = file.info(bindingSrc)
if not bindingSrcInfo or not bindingSrcInfo.isDirectory then
	print "Unable to read " + bindingSrc
	exit
end if

// Prepare the output buffer (outLines).
outLines = []

// Scan the Raylib files for function notes.
platformFolder = file.child(raylibSrc, "platforms")
for child in file.children(platformFolder)
	if child[0] != "r" or child[-2:] != ".c" then continue
	processRaylibFile file.child(platformFolder, child)
end for
for child in file.children(raylibSrc)
	if child[0] != "r" or (child[-2:] != ".c" and child[-2:] != ".h") then continue
	processRaylibFile file.child(raylibSrc, child)
end for


// Now, process each file of the form R*.cpp in our src directory
for child in file.children(bindingSrc)
	if child[0] != "R" or child[-4:] != ".cpp" then continue
//	if child != "RMath.cpp" then continue
	processBindingFile file.child(bindingSrc, child)
end for

// Finally, write the output.
outPath = "API_DOC.md"
file.writeLines outPath, outLines
print "Wrote " + outLines.len + " lines to " + outPath