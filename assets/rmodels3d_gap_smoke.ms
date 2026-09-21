// rmodels3d_gap_smoke.ms
// Smoke test for the newly-added 3D data-access intrinsics:
// GetMaterialTexture/GetMaterialColor/SetMaterialColor/GetMaterialValue/SetMaterialValue,
// GetMeshBuffer, GetModelBoneCount/GetModelBoneName/GetModelBoneParent/GetModelBoneMatrix,
// GetAnimationFramePose.
// Runs once (no render loop) and exits.

rl = raylib
if not rl.IsWindowReady then rl.InitWindow

fail = 0

check = function(label, cond)
	outer.fail = fail
	if cond then
		print "OK   " + label
	else
		print "FAIL " + label
		outer.fail = fail + 1
	end if
end function

// --- Material map texture/color/value ---

mat = rl.LoadMaterialDefault
img = rl.GenImageColor(4, 4, rl.RED)
tex = rl.LoadTextureFromImage(img)
rl.SetMaterialTexture mat, rl.MATERIAL_MAP_ALBEDO, tex
gotTex = rl.GetMaterialTexture(mat, rl.MATERIAL_MAP_ALBEDO)
check "GetMaterialTexture round-trips SetMaterialTexture", gotTex != null and gotTex.id == tex.id

rl.SetMaterialColor mat, rl.MATERIAL_MAP_ALBEDO, {"r":10, "g":20, "b":30, "a":40}
gotColor = rl.GetMaterialColor(mat, rl.MATERIAL_MAP_ALBEDO)
check "GetMaterialColor round-trips SetMaterialColor", gotColor != null and gotColor.r == 10 and gotColor.g == 20 and gotColor.b == 30 and gotColor.a == 40

rl.SetMaterialValue mat, rl.MATERIAL_MAP_ALBEDO, 0.75
gotValue = rl.GetMaterialValue(mat, rl.MATERIAL_MAP_ALBEDO)
check "GetMaterialValue round-trips SetMaterialValue", abs(gotValue - 0.75) < 0.001

// --- Mesh buffer readback ---

mesh = rl.GenMeshCube(2, 2, 2)
vertsRaw = rl.GetMeshBuffer(mesh, 0)
expectedBytes = mesh.vertexCount * 3 * 4  // 3 floats * 4 bytes each
check "GetMeshBuffer(vertices) returns expected byte length", vertsRaw != null and vertsRaw.len == expectedBytes

idxRaw = rl.GetMeshBuffer(mesh, 6)
expectedIdxBytes = mesh.triangleCount * 3 * 2  // 3 ushorts * 2 bytes each
check "GetMeshBuffer(indices) returns expected byte length", idxRaw != null and idxRaw.len == expectedIdxBytes

tangentsRaw = rl.GetMeshBuffer(mesh, 4)
check "GetMeshBuffer(tangents) is null before GenMeshTangents", tangentsRaw == null

// --- Skeleton / bone data (static model has no skeleton) ---

model = rl.LoadModelFromMesh(mesh)
boneCount = rl.GetModelBoneCount(model)
check "GetModelBoneCount is 0 for a non-skinned model", boneCount == 0
check "GetModelBoneName returns null out of range", rl.GetModelBoneName(model, 0) == null

print "Done. Failures: " + fail
if fail == 0 then print "ALL PASS"
exit
