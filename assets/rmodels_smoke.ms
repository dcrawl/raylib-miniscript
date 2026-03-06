// rmodels_smoke.ms
// Quick runtime smoke test for RModels bindings.
//
// Exercises:
// 1) GenMeshCube
// 2) LoadModelFromMesh
// 3) DrawModel / DrawBoundingBox
// 4) Collision APIs (sphere/box/ray)

rl = raylib

v3 = function(x=0, y=0, z=0)
	return {"x": x, "y": y, "z": z}
end function

boolText = function(b)
	if b then return "true"
	return "false"
end function

identityMatrix = function
	return {
		"m0": 1, "m1": 0, "m2": 0, "m3": 0,
		"m4": 0, "m5": 1, "m6": 0, "m7": 0,
		"m8": 0, "m9": 0, "m10": 1, "m11": 0,
		"m12": 0, "m13": 0, "m14": 0, "m15": 1
	}
end function

run = function
	rl.SetWindowTitle "RModels Smoke"
	rl.SetTargetFPS 60

	mesh = rl.GenMeshCube(2, 2, 2)
	if mesh == null then
		print "GenMeshCube failed"
		return
	end if

	model = rl.LoadModelFromMesh(mesh)
	if model == null then
		print "LoadModelFromMesh failed"
		return
	end if

	modelPos = v3(0, 0, 0)
	modelBounds = rl.GetModelBoundingBox(model)
	staticSphere = v3(0, 0, 0)
	ray = {
		"position": v3(-5, 0.6, 0),
		"direction": v3(1, 0, 0),
	}

	print "RModels smoke script started. Press ESC to stop."
	t = 0
	lastLoggedSecond = -1

	while true
		if rl.IsKeyPressed(rl.KEY_ESCAPE) then break

		dt = rl.GetFrameTime
		t += dt

		movingX = sin(t) * 2.5
		movingSphere = v3(movingX, 0, 0)
		movingBox = {
			"min": v3(movingX - 0.75, -1, -1),
			"max": v3(movingX + 0.75, 1, 1),
		}
		ray.position = v3(-5, 0.6 + sin(t * 1.7) * 0.6, 0)

		hitSpheres = rl.CheckCollisionSpheres(movingSphere, 1.2, staticSphere, 1.0)
		hitBoxes = rl.CheckCollisionBoxes(modelBounds, movingBox)
		hitBoxSphere = rl.CheckCollisionBoxSphere(modelBounds, movingSphere, 0.8)
		hitRayBox = rl.GetRayCollisionBox(ray, modelBounds)
		hitRayMesh = rl.GetRayCollisionMesh(ray, mesh, identityMatrix)

		thisSecond = floor(t)
		if thisSecond != lastLoggedSecond then
			lastLoggedSecond = thisSecond
			print "[rmodels] spheres=" + boolText(hitSpheres)
			print "[rmodels] boxes=" + boolText(hitBoxes)
			print "[rmodels] boxSphere=" + boolText(hitBoxSphere)
			print "[rmodels] rayBox.hit=" + boolText(hitRayBox.hit) + " dist=" + hitRayBox.distance
			print "[rmodels] rayMesh.hit=" + boolText(hitRayMesh.hit) + " dist=" + hitRayMesh.distance
		end if

		rl.BeginDrawing
		rl.ClearBackground rl.BLACK

		// This validates DrawModel call path; scene camera support is currently limited.
		rl.DrawModel(model, modelPos, 1, rl.WHITE)
		if hitBoxSphere then
			rl.DrawBoundingBox(modelBounds, rl.RED)
		else
			rl.DrawBoundingBox(modelBounds, rl.GREEN)
		end if

		rl.DrawText("RModels smoke test", 20, 20, 24, rl.YELLOW)
		rl.DrawText("ESC: exit", 20, 50, 18, rl.RAYWHITE)
		rl.DrawText("CheckCollisionSpheres: " + boolText(hitSpheres), 20, 82, 16, rl.RAYWHITE)
		rl.DrawText("CheckCollisionBoxes: " + boolText(hitBoxes), 20, 104, 16, rl.RAYWHITE)
		rl.DrawText("CheckCollisionBoxSphere: " + boolText(hitBoxSphere), 20, 126, 16, rl.RAYWHITE)
		rl.DrawText("GetRayCollisionBox.hit: " + boolText(hitRayBox.hit), 20, 148, 16, rl.RAYWHITE)
		rl.DrawText("GetRayCollisionMesh.hit: " + boolText(hitRayMesh.hit), 20, 170, 16, rl.RAYWHITE)
		rl.DrawFPS

		rl.EndDrawing
		yield
	end while

	rl.UnloadModel(model)
	print "RModels smoke script finished"
end function

if locals == globals then run
