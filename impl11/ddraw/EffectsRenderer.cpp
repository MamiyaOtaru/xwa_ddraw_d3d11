#include "EffectsRenderer.h"

#ifdef _DEBUG
#include "../Debug/RTShadowCS.h"
#else
#include "../Release/RTShadowCS.h"
#endif

// DEBUG vars
int g_iD3DExecuteCounter = 0, g_iD3DExecuteCounterSkipHi = -1, g_iD3DExecuteCounterSkipLo = -1;

// Control vars
bool g_bEnableAnimations = true;
bool g_bRTEnabled = false;

// Maps an ObjectId to its index in the ObjectEntry table.
// Textures have an associated objectId, this map tells us the slot
// in the objects array where we'll find the corresponding objectId.
std::map<int, int> g_objectIdToIndex;
// The new per-craft events will only store hull events (and maybe sys/disabled events
// later), but we need a previous and current event, and a timer (for animations).

// Maps an ObjectId to its InstanceEvent
std::map<uint64_t, InstanceEvent> g_objectIdToInstanceEvent;

EffectsRenderer *g_effects_renderer = nullptr;

Matrix4 GetSimpleDirectionMatrix(Vector4 Fs, bool invert);

float4 TransformProjection(float3 input)
{
	float vpScaleX = g_VSCBuffer.viewportScale[0];
	float vpScaleY = g_VSCBuffer.viewportScale[1];
	float vpScaleZ = g_VSCBuffer.viewportScale[2];
	float projectionValue1 = *(float*)0x08B94CC; // Znear
	float projectionValue2 = *(float*)0x05B46B4; // Zfar
	float projectionDeltaX = *(float*)0x08C1600 + *(float*)0x0686ACC;
	float projectionDeltaY = *(float*)0x080ACF8 + *(float*)0x07B33C0 + *(float*)0x064D1AC;

	float4 pos;
	// st0 = Znear / input.z == pos.w
	float st0 = projectionValue1 / input.z; // st0 = Znear / MetricZ
	pos.x = input.x * st0 + projectionDeltaX;
	pos.y = input.y * st0 + projectionDeltaY;
	// pos.z = (st0 * Zfar/32) / (abs(st0) * Zfar/32 + Znear/3) * 0.5
	pos.z = (st0 * projectionValue2 / 32) / (abs(st0) * projectionValue2 / 32 + projectionValue1 / 3) * 0.5f;
	pos.w = 1.0f;
	pos.x = (pos.x * vpScaleX - 1.0f) * vpScaleZ;
	pos.y = (pos.y * vpScaleY + 1.0f) * vpScaleZ;
	// We previously did pos.w = 1. After the next line, pos.w = 1 / st0, that implies
	// that pos.w == rhw and st0 == w
	pos.x *= 1.0f / st0;
	pos.y *= 1.0f / st0;
	pos.z *= 1.0f / st0;
	pos.w *= 1.0f / st0;

	return pos;
}

/*
 * Same as TransformProjection, but returns screen coordinates + depth buffer
 */
float4 TransformProjectionScreen(float3 input)
{
	float4 pos = TransformProjection(input);
	float w = pos.w;
	// Apply the division by w that DirectX does implicitly
	pos.x = pos.x / w;
	pos.y = pos.y / w;
	//pos.z = pos.z / w;
	//pos.w = 1.0f;
	// pos.xy is now in the range -1..1, convert to screen coords
	// TODO: What should I do with vpScaleZ?
	pos.x = (pos.x + 1.0f) / g_VSCBuffer.viewportScale[0];
	pos.y = (pos.y - 1.0f) / g_VSCBuffer.viewportScale[1];
	return pos;
}

/*
 * Back-projects a 2D point in normalized DirectX coordinates (-1..1) into an OPT-scale 3D point
 */
float3 InverseTransformProjection(float4 input)
{
	float3 pos;
	float vpScaleX = g_VSCBuffer.viewportScale[0];
	float vpScaleY = g_VSCBuffer.viewportScale[1];
	float vpScaleZ = g_VSCBuffer.viewportScale[2];
	float projectionValue1 = *(float*)0x08B94CC; // Znear
	//float projectionValue2 = *(float*)0x05B46B4; // Zfar
	float projectionDeltaX = *(float*)0x08C1600 + *(float*)0x0686ACC;
	float projectionDeltaY = *(float*)0x080ACF8 + *(float*)0x07B33C0 + *(float*)0x064D1AC;

	float st0 = 1.0f / input.w;
	// input.w = 1 / st0 = pos.z / projectionValue1
	// input.w = pos.z / projectionValue1
	// input.w * projectionValue1 = pos.z
	pos.z = projectionValue1 * input.w;
	// pos.z is now OPT-scale

	// Reverse the premultiplication by w:
	pos.x = input.x * st0;
	pos.y = input.y * st0;

	// Convert from screen coords to normalized coords
	pos.x = (pos.x / vpScaleZ + 1.0f) / vpScaleX;
	pos.y = (pos.y / vpScaleZ - 1.0f) / vpScaleY;

	pos.x = (pos.x - projectionDeltaX) / st0;
	pos.y = (pos.y - projectionDeltaY) / st0;
	// input.xyz is now OPT-scale

	return pos;
}

/*
 * Converts 2D into metric 3D at OPT-scale (you get 100% metric 3D by multiplying with OPT_TO_METERS after this)
 * The formulas used here work with the engine glow and with the explosions because w == z in that case.
 * It may not work in other cases.
 * Confirmed cases: Engine glow, Explosions.
 */
float3 InverseTransformProjectionScreen(float4 input)
{
	float Znear = *(float *)0x08B94CC;
	float Zfar = *(float *)0x05B46B4;
	float projectionDeltaX = *(float*)0x08C1600 + *(float*)0x0686ACC;
	float projectionDeltaY = *(float*)0x080ACF8 + *(float*)0x07B33C0 + *(float*)0x064D1AC;
	//float st0 = input.w;
	// st0 = Znear / MetricZ == pos.w

	// depthZ = (st0 * Zfar / 32.0f) / (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 0.5f;
	// depthZ * 2.0f = (st0 * Zfar / 32.0f) / (abs(st0) * Zfar / 32.0f + Znear / 3.0f);
	// depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) = st0 * Zfar / 32.0f;
	// depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 32.0f = st0 * Zfar;
	// depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 32.0f / Zfar = st0;
	// st0 = depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 32.0f / Zfar;

	// The engine glow has z = w!!! So we must also do:
	// st0 = Znear / (Zfar / input.pos.w - Zfar);
	// Znear / st0 = Zfar / input.pos.w - Zfar
	// Znear / st0 + Zfar = Zfar / input.pos.w
	// Zfar / (Znear / st0 + Zfar) = input.pos.w
	// input.pos.w = Zfar / (Znear / st0 + Zfar)

	// Znear / MetricZ = depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 32.0f / Zfar;
	// MetricZ = Znear / (depthZ * 2.0f * (abs(st0) * Zfar / 32.0f + Znear / 3.0f) * 32.0f / Zfar);

	float vpScaleX = g_VSCBuffer.viewportScale[0];
	float vpScaleY = g_VSCBuffer.viewportScale[1];
	float vpScaleZ = g_VSCBuffer.viewportScale[2];
	// input.xy is in screen coords (0,0)-(W,H), convert to normalized DirectX: -1..1
	input.x = (input.x * vpScaleX) - 1.0f;
	input.y = (input.y * vpScaleY) + 1.0f;
	// input.xy is now in the range -1..1, invert the formulas in TransformProjection:
	input.x = (input.x / vpScaleZ + 1.0f) / vpScaleX;
	input.y = (input.y / vpScaleZ - 1.0f) / vpScaleY;

	/*
	The next step, in TransformProjection() is to recover metric Z from sz (depthZ).
	However, in this case we must make a detour. From the VertexShader, we see this:

	if (input.pos.z == input.pos.w)
	{
		float z = s_V0x05B46B4 / input.pos.w - s_V0x05B46B4;
		st0 = s_V0x08B94CC / z;
	}

	The engine glow has w == z, so we must use those formulas. Also, that temporary z is the
	Metric Z we're looking for, so the formula is straightforward now:
	*/
	float3 P;
	if (input.z == input.w)
		P.z = Zfar / input.w - Zfar;
	else
		// For the regular case, when w != z, we can probably just invert st0 from TransformProjection():
		P.z = Znear / input.w;
		//P.z = Znear * input.w;
		//P.z = input.w;

	// We can now continue inverting the formulas in TransformProjection
	float st0 = Znear / P.z;

	// Continue inverting the formulas in TransformProjection:
	// input.x = P.x * st0 + projectionDeltaX;
	// input.y = P.y * st0 + projectionDeltaY;
	P.x = (input.x - projectionDeltaX) / st0;
	P.y = (input.y - projectionDeltaY) / st0;
	// P is now metric * OPT-scale
	return P;
}


void IncreaseD3DExecuteCounterSkipHi(int Delta) {
	g_iD3DExecuteCounterSkipHi += Delta;
	if (g_iD3DExecuteCounterSkipHi < -1)
		g_iD3DExecuteCounterSkipHi = -1;
	log_debug("[DBG] g_iD3DExecuteCounterSkip, Lo: %d, Hi: %d", g_iD3DExecuteCounterSkipLo, g_iD3DExecuteCounterSkipHi);
}

void IncreaseD3DExecuteCounterSkipLo(int Delta) {
	g_iD3DExecuteCounterSkipLo += Delta;
	if (g_iD3DExecuteCounterSkipLo < -1)
		g_iD3DExecuteCounterSkipLo = -1;
	log_debug("[DBG] g_iD3DExecuteCounterSkip, Lo: %d, Hi: %d", g_iD3DExecuteCounterSkipLo, g_iD3DExecuteCounterSkipHi);
}

void ResetObjectIndexMap() {
	g_objectIdToIndex.clear();
	g_objectIdToInstanceEvent.clear();
}

// ****************************************************
// Dump to OBJ
// ****************************************************
// Set the following flag to true to enable dumping the current scene to an OBJ file
bool bD3DDumpOBJEnabled = false;
bool bHangarDumpOBJEnabled = false;
FILE *D3DDumpOBJFile = NULL, *D3DDumpLaserOBJFile = NULL;
int D3DOBJFileIdx = 0, D3DTotalVertices = 0, D3DTotalNormals = 0, D3DOBJGroup = 0;
int D3DOBJLaserFileIdx = 0, D3DTotalLaserVertices = 0, D3DTotalLaserTextureVertices = 0, D3DOBJLaserGroup = 0;

void OBJDump(XwaVector3 *vertices, int count)
{
	static int obj_idx = 1;
	log_debug("[DBG] OBJDump, count: %d, obj_idx: %d", count, obj_idx);

	fprintf(D3DDumpOBJFile, "o obj-%d\n", obj_idx++);
	for (int index = 0; index < count; index++) {
		XwaVector3 v = vertices[index];
		fprintf(D3DDumpOBJFile, "v %0.6f %0.6f %0.6f\n", v.x, v.y, v.z);
	}
	fprintf(D3DDumpOBJFile, "\n");
}

inline Matrix4 XwaTransformToMatrix4(const XwaTransform &M)
{
	return Matrix4(
		M.Rotation._11, M.Rotation._12, M.Rotation._13, 0.0f,
		M.Rotation._21, M.Rotation._22, M.Rotation._23, 0.0f,
		M.Rotation._31, M.Rotation._32, M.Rotation._33, 0.0f,
		M.Position.x, M.Position.y, M.Position.z, 1.0f
	);
}

inline Vector4 XwaVector3ToVector4(const XwaVector3 &V)
{
	return Vector4(V.x, V.y, V.z, 1.0f);
}

inline Vector2 XwaTextureVertexToVector2(const XwaTextureVertex &V)
{
	return Vector2(V.u, V.v);
}

/*
 * Dump the current Limits to an OBJ file.
 * Before calling this method, make sure you call UpdateLimits() to convert the internal aabb into
 * a list of vertices and then call TransformLimits() with the appropriate transform matrix.
 */
void AABB::DumpLimitsToOBJ(FILE *D3DDumpOBJFile, int OBJGroupId, int VerticesCountOffset)
{
	fprintf(D3DDumpOBJFile, "o aabb-%d\n", OBJGroupId);
	for (uint32_t i = 0; i < Limits.size(); i++) {
		Vector4 V = Limits[i];
		fprintf(D3DDumpOBJFile, "v %0.6f %0.6f %0.6f\n", V.x, V.y, V.z);
	}

	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 0, VerticesCountOffset + 1);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 1, VerticesCountOffset + 2);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 2, VerticesCountOffset + 3);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 3, VerticesCountOffset + 0);

	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 4, VerticesCountOffset + 5);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 5, VerticesCountOffset + 6);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 6, VerticesCountOffset + 7);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 7, VerticesCountOffset + 4);

	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 0, VerticesCountOffset + 4);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 1, VerticesCountOffset + 5);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 2, VerticesCountOffset + 6);
	fprintf(D3DDumpOBJFile, "f %d %d\n", VerticesCountOffset + 3, VerticesCountOffset + 7);
	fprintf(D3DDumpOBJFile, "\n");
}

void EffectsRenderer::OBJDumpD3dVertices(const SceneCompData *scene, const Matrix4 &A)
{
	std::ostringstream str;
	XwaVector3 *MeshVertices = scene->MeshVertices;
	int MeshVerticesCount = *(int*)((int)scene->MeshVertices - 8);
	XwaVector3* MeshNormals = scene->MeshVertexNormals;
	int MeshNormalsCount = *(int*)((int)MeshNormals - 8);
	static XwaVector3 *LastMeshVertices = nullptr;
	static int LastMeshVerticesCount = 0, LastMeshNormalsCount = 0;
	bool bShadowDump = g_rendererType == RendererType_Shadow;

	if (D3DOBJGroup == 1) {
		LastMeshVerticesCount = 0;
		LastMeshNormalsCount = 0;
	}

	float *Znear = (float *)0x08B94CC;
	float *Zfar = (float *)0x05B46B4;
	float projectionDeltaX = *(float*)0x08C1600 + *(float*)0x0686ACC;
	float projectionDeltaY = *(float*)0x080ACF8 + *(float*)0x07B33C0 + *(float*)0x064D1AC;
	fprintf(D3DDumpOBJFile, "# (Znear) 0x08B94CC: %0.6f, (Zfar) 0x05B46B4: %0.6f\n", *Znear, *Zfar);
	fprintf(D3DDumpOBJFile, "# projDeltaX,Y: %0.6f, %0.6f\n", projectionDeltaX, projectionDeltaY);
	fprintf(D3DDumpOBJFile, "# viewportScale: %0.6f, %0.6f %0.6f\n",
		g_VSCBuffer.viewportScale[0], g_VSCBuffer.viewportScale[1], g_VSCBuffer.viewportScale[2]);
	if (bShadowDump) {
		fprintf(D3DDumpOBJFile, "# Hangar Shadow Render, Floor: %0.3f, hangarShadowAccStartEnd: %0.3f, "
			"sx1,sy1: (%0.3f, %0.3f), sx2,sy2:(%0.3f, %0.3f)\n",
			_constants.floorLevel, _constants.hangarShadowAccStartEnd,
			_constants.sx1, _constants.sy1,
			_constants.sx2, _constants.sy2);
		fprintf(D3DDumpOBJFile, "# Camera: %0.3f, %0.3f\n",
			_constants.cameraPositionX, _constants.cameraPositionY);
	}

	if (LastMeshVertices != MeshVertices) {
		// This is a new mesh, dump all the vertices.
		Matrix4 W = XwaTransformToMatrix4(scene->WorldViewTransform);
		Matrix4 L, S1, S2;
		L.identity();
		S1.scale(OPT_TO_METERS, -OPT_TO_METERS, OPT_TO_METERS);
		S2.scale(METERS_TO_OPT, -METERS_TO_OPT, METERS_TO_OPT);
		if (g_rendererType == RendererType_Shadow)
			// See HangarShadowSceneHook for an explanation of why L looks like this:
			L = A * S1 * Matrix4(_constants.hangarShadowView) * S2;

#define DUMP_AABBS 0
#if DUMP_AABBS == 1
		auto it = _AABBs.find((int)(scene->MeshVertices));
		if (it != _AABBs.end())
		{
			AABB aabb = it->second;
			aabb.UpdateLimits();
			aabb.TransformLimits(L * S1 * W);
			aabb.DumpLimitsToOBJ(D3DDumpOBJFile, D3DOBJGroup, D3DTotalVertices + LastMeshVerticesCount);
			D3DTotalVertices += aabb.Limits.size();
		}
		else
			fprintf(D3DDumpOBJFile, "# No AABB found for this mesh\n");
#endif

		//log_debug("[DBG] Writting obj_idx: %d, MeshVerticesCount: %d, NormalsCount: %d, FacesCount: %d",
		//	D3DOBJGroup, MeshVerticesCount, MeshNormalsCount, scene->FacesCount);
		fprintf(D3DDumpOBJFile, "o %s-%d\n", bShadowDump ? "shw" : "obj", D3DOBJGroup);
		for (int i = 0; i < MeshVerticesCount; i++) {
			XwaVector3 v = MeshVertices[i];
			Vector4 V(v.x, v.y, v.z, 1.0f);
			V = W * V;

			// Enable the following block to debug InverseTransformProjection
#define EXTRA_DEBUG 0
#if EXTRA_DEBUG == 1
			{
				float3 P;
				P.x = V.x;
				P.y = V.y;
				P.z = V.z;
				float4 Q = TransformProjectionScreen(P);
				float3 R = InverseTransformProjectionScreen(Q);
				R.x *= OPT_TO_METERS;
				R.y *= OPT_TO_METERS;
				R.z *= OPT_TO_METERS;
				fprintf(D3DDumpOBJFile, "# Q %0.3f %0.3f %0.6f %0.6f\n", Q.x, Q.y, Q.z, Q.w);
				fprintf(D3DDumpOBJFile, "# R %0.3f %0.3f %0.6f\n", R.x, R.y, R.z);

				//float4 Q = TransformProjection(P);
				//float3 R = InverseTransformProjection(Q);
				//fprintf(D3DDumpOBJFile, "# Q %0.3f %0.3f %0.6f %0.6f\n", Q.x, Q.y, Q.z, Q.w);
				//fprintf(D3DDumpOBJFile, "# R %0.3f %0.3f %0.3f\n", R.x, R.y, R.z);
				//fprintf(D3DDumpOBJFile, "# V %0.3f %0.3f %0.3f\n", V.x, V.y, V.z);
			}
#endif
			// OPT to meters conversion:
			//V *= OPT_TO_METERS;
			V = L * S1 * V;
			fprintf(D3DDumpOBJFile, "v %0.6f %0.6f %0.6f\n", V.x, V.y, V.z);
		}
		fprintf(D3DDumpOBJFile, "\n");

		// Dump the normals
		for (int i = 0; i < MeshNormalsCount; i++) {
			XwaVector3 N = MeshNormals[i];
			fprintf(D3DDumpOBJFile, "vn %0.6f %0.6f %0.6f\n", N.x, N.y, N.z);
		}
		fprintf(D3DDumpOBJFile, "\n");

		D3DTotalVertices += LastMeshVerticesCount;
		D3DTotalNormals += LastMeshNormalsCount;
		D3DOBJGroup++;

		LastMeshVertices = MeshVertices;
		LastMeshVerticesCount = MeshVerticesCount;
		LastMeshNormalsCount = MeshNormalsCount;
	}

	// The following works alright, but it's not how things are rendered.
	for (int faceIndex = 0; faceIndex < scene->FacesCount; faceIndex++) {
		OptFaceDataNode_01_Data_Indices& faceData = scene->FaceIndices[faceIndex];
		int edgesCount = faceData.Edge[3] == -1 ? 3 : 4;
		std::string line = "f ";

		for (int vertexIndex = 0; vertexIndex < edgesCount; vertexIndex++)
		{
			// faceData.Vertex[vertexIndex] matches the vertex index data from the OPT
			line += std::to_string(faceData.Vertex[vertexIndex] + D3DTotalVertices) + "//" +
					std::to_string(faceData.VertexNormal[vertexIndex] + D3DTotalNormals) + " ";
		}
		fprintf(D3DDumpOBJFile, "%s\n", line.c_str());
	}
	fprintf(D3DDumpOBJFile, "\n");
}

//************************************************************************
// Effects Renderer
//************************************************************************

EffectsRenderer::EffectsRenderer() : D3dRenderer() {
	_hangarShadowMapRotation.identity();
	_hangarShadowMapRotation.rotateX(180.0f);
}

void EffectsRenderer::CreateShaders() {
	ID3D11Device* device = _deviceResources->_d3dDevice;

	D3dRenderer::CreateShaders();

	if (g_bRTEnabled) {
		device->CreateComputeShader(g_RTShadowCS, sizeof(g_RTShadowCS), nullptr, &_RTShadowCS);
	}
}

void EffectsRenderer::SceneBegin(DeviceResources* deviceResources)
{
	D3dRenderer::SceneBegin(deviceResources);

	// Reset any deferred-rendering variables here
	_LaserDrawCommands.clear();
	_TransparentDrawCommands.clear();
	_ShadowMapDrawCommands.clear();
	_bCockpitConstantsCaptured = false;
	_bShadowsRenderedInCurrentFrame = false;
	_bHangarShadowsRenderedInCurrentFrame = false;
	// Initialize the joystick mesh transform on this frame
	_bJoystickTransformReady = false;
	//_bThrottleTransformReady = false;
	//_bThrottleRotAxisToZPlusReady = false;
	_joystickMeshTransform.identity();
	// Initialize the mesh transform for this frame
	g_OPTMeshTransformCB.MeshTransform.identity();
	deviceResources->InitVSConstantOPTMeshTransform(
		deviceResources->_OPTMeshTransformCB.GetAddressOf(), &g_OPTMeshTransformCB);
	// Initialize the hangar AABB
	_hangarShadowAABB.SetInfinity();

	if (PlayerDataTable->missionTime == 0)
		ApplyCustomHUDColor();

	// Initialize the OBJ dump file for the current frame
	if ((bD3DDumpOBJEnabled || bHangarDumpOBJEnabled) && g_bDumpSSAOBuffers) {
		// Create the file if it doesn't exist
		if (D3DDumpOBJFile == NULL) {
			char sFileName[128];
			sprintf_s(sFileName, 128, "d3dcapture-%d.obj", D3DOBJFileIdx);
			fopen_s(&D3DDumpOBJFile, sFileName, "wt");
		}
		// Reset the vertex counter and group
		D3DTotalVertices = 1;
		D3DTotalNormals = 1;
		D3DOBJGroup = 1;

		if (D3DDumpLaserOBJFile == NULL) {
			char sFileName[128];
			sprintf_s(sFileName, 128, "d3dlasers-%d.obj", D3DOBJLaserFileIdx);
			fopen_s(&D3DDumpLaserOBJFile, sFileName, "wt");
		}
		D3DTotalLaserVertices = 1;
		D3DTotalLaserTextureVertices = 1;
		D3DOBJLaserGroup = 1;
	}
}

void EffectsRenderer::SceneEnd()
{
	D3dRenderer::SceneEnd();

	// Close the OBJ dump file for the current frame
	if ((bD3DDumpOBJEnabled || bHangarDumpOBJEnabled) && g_bDumpSSAOBuffers) {
		fclose(D3DDumpOBJFile); D3DDumpOBJFile = NULL;
		log_debug("[DBG] OBJ file [d3dcapture-%d.obj] written", D3DOBJFileIdx);
		D3DOBJFileIdx++;

		fclose(D3DDumpLaserOBJFile); D3DDumpLaserOBJFile = NULL;
		log_debug("[DBG] OBJ file [d3dlasers-%d.obj] written", D3DOBJLaserFileIdx);
		D3DOBJLaserFileIdx++;

		if (!_hangarShadowAABB.IsInvalid() && bHangarDumpOBJEnabled) {
			FILE *ShadowMapFile = NULL;
			fopen_s(&ShadowMapFile, ".\\HangarShadowMapLimits.OBJ", "wt");
			_hangarShadowAABB.UpdateLimits();
			// The hangar AABB should already be in metric space and in the light view frame. There's no need
			// to apply additional transforms here.
			_hangarShadowAABB.DumpLimitsToOBJ(ShadowMapFile, 1, 1);
			fclose(ShadowMapFile);
		}
	}
}

/* Function to quickly enable/disable ZWrite. */
HRESULT EffectsRenderer::QuickSetZWriteEnabled(BOOL Enabled)
{
	HRESULT hr;
	auto &resources = _deviceResources;
	auto &device = resources->_d3dDevice;

	D3D11_DEPTH_STENCIL_DESC desc;
	_solidDepthState->GetDesc(&desc);
	ComPtr<ID3D11DepthStencilState> depthState;

	desc.DepthEnable = Enabled;
	desc.DepthWriteMask = Enabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	desc.DepthFunc = Enabled ? D3D11_COMPARISON_GREATER : D3D11_COMPARISON_ALWAYS;
	desc.StencilEnable = FALSE;
	hr = resources->_d3dDevice->CreateDepthStencilState(&desc, &depthState);
	if (SUCCEEDED(hr))
		resources->_d3dDeviceContext->OMSetDepthStencilState(depthState, 0);
	return hr;
}

inline void EffectsRenderer::EnableTransparency() {
	auto& resources = _deviceResources;
	D3D11_BLEND_DESC blendDesc{};

	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	resources->InitBlendState(nullptr, &blendDesc);
}

inline void EffectsRenderer::EnableHoloTransparency() {
	auto& resources = _deviceResources;
	D3D11_BLEND_DESC blendDesc{};

	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	resources->InitBlendState(nullptr, &blendDesc);
}

void EffectsRenderer::SaveContext()
{
	auto &context = _deviceResources->_d3dDeviceContext;

	_oldVSCBuffer = g_VSCBuffer;
	_oldPSCBuffer = g_PSCBuffer;
	_oldDCPSCBuffer = g_DCPSCBuffer;

	context->VSGetConstantBuffers(0, 1, _oldVSConstantBuffer.GetAddressOf());
	context->PSGetConstantBuffers(0, 1, _oldPSConstantBuffer.GetAddressOf());

	context->VSGetShaderResources(0, 3, _oldVSSRV[0].GetAddressOf());
	context->PSGetShaderResources(0, 13, _oldPSSRV[0].GetAddressOf());

	context->VSGetShader(_oldVertexShader.GetAddressOf(), nullptr, nullptr);
	// TODO: Use GetCurrentPixelShader here instead of PSGetShader and do *not* Release()
	// _oldPixelShader in RestoreContext
	context->PSGetShader(_oldPixelShader.GetAddressOf(), nullptr, nullptr);

	context->PSGetSamplers(0, 2, _oldPSSamplers[0].GetAddressOf());

	context->OMGetRenderTargets(8, _oldRTVs[0].GetAddressOf(), _oldDSV.GetAddressOf());
	context->OMGetDepthStencilState(_oldDepthStencilState.GetAddressOf(), &_oldStencilRef);
	context->OMGetBlendState(_oldBlendState.GetAddressOf(), _oldBlendFactor, &_oldSampleMask);

	context->IAGetInputLayout(_oldInputLayout.GetAddressOf());
	context->IAGetPrimitiveTopology(&_oldTopology);
	context->IAGetVertexBuffers(0, 1, _oldVertexBuffer.GetAddressOf(), &_oldStride, &_oldOffset);
	context->IAGetIndexBuffer(_oldIndexBuffer.GetAddressOf(), &_oldFormat, &_oldIOffset);

	_oldNumViewports = 2;
	context->RSGetViewports(&_oldNumViewports, _oldViewports);
}

void EffectsRenderer::RestoreContext()
{
	auto &resources = _deviceResources;
	auto &context = _deviceResources->_d3dDeviceContext;

	// Restore a previously-saved context
	g_VSCBuffer = _oldVSCBuffer;
	g_PSCBuffer = _oldPSCBuffer;
	g_DCPSCBuffer = _oldDCPSCBuffer;
	resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
	resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);
	resources->InitPSConstantBufferDC(resources->_PSConstantBufferDC.GetAddressOf(), &g_DCPSCBuffer);

	// The hyperspace effect needs the current VS constants to work properly
	if (g_HyperspacePhaseFSM == HS_INIT_ST)
		context->VSSetConstantBuffers(0, 1, _oldVSConstantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _oldPSConstantBuffer.GetAddressOf());

	context->VSSetShaderResources(0, 3, _oldVSSRV[0].GetAddressOf());
	context->PSSetShaderResources(0, 13, _oldPSSRV[0].GetAddressOf());

	// It's important to use the Init*Shader methods here, or the shaders won't be
	// applied sometimes.
	resources->InitVertexShader(_oldVertexShader);
	resources->InitPixelShader(_oldPixelShader);

	context->PSSetSamplers(0, 2, _oldPSSamplers[0].GetAddressOf());
	context->OMSetRenderTargets(8, _oldRTVs[0].GetAddressOf(), _oldDSV.Get());
	context->OMSetDepthStencilState(_oldDepthStencilState.Get(), _oldStencilRef);
	context->OMSetBlendState(_oldBlendState.Get(), _oldBlendFactor, _oldSampleMask);

	resources->InitInputLayout(_oldInputLayout);
	resources->InitTopology(_oldTopology);
	context->IASetVertexBuffers(0, 1, _oldVertexBuffer.GetAddressOf(), &_oldStride, &_oldOffset);
	context->IASetIndexBuffer(_oldIndexBuffer.Get(), _oldFormat, _oldIOffset);

	context->RSSetViewports(_oldNumViewports, _oldViewports);

	// Release everything. Previous calls to *Get* increase the refcount
	_oldVSConstantBuffer.Release();
	_oldPSConstantBuffer.Release();

	for (int i = 0; i < 3; i++)
		_oldVSSRV[i].Release();
	for (int i = 0; i < 13; i++)
		_oldPSSRV[i].Release();

	_oldVertexShader.Release();
	_oldPixelShader.Release();

	for (int i = 0; i < 2; i++)
		_oldPSSamplers[i].Release();

	for (int i = 0; i < 8; i++)
		_oldRTVs[i].Release();
	_oldDSV.Release();
	_oldDepthStencilState.Release();
	_oldBlendState.Release();
	_oldInputLayout.Release();
	_oldVertexBuffer.Release();
	_oldIndexBuffer.Release();
}

void EffectsRenderer::UpdateTextures(const SceneCompData* scene)
{
	const unsigned char ShipCategory_PlayerProjectile = 6;
	const unsigned char ShipCategory_OtherProjectile = 7;
	unsigned char category = scene->pObject->ShipCategory;
	bool isProjectile = category == ShipCategory_PlayerProjectile || category == ShipCategory_OtherProjectile;

	const auto XwaD3dTextureCacheUpdateOrAdd = (void(*)(XwaTextureSurface*))0x00597784;
	const auto L00432750 = (short(*)(unsigned short, short, short))0x00432750;
	const ExeEnableEntry* s_ExeEnableTable = (ExeEnableEntry*)0x005FB240;

	XwaTextureSurface* surface = nullptr;
	XwaTextureSurface* surface2 = nullptr;

	_constants.renderType = 0;
	_constants.renderTypeIllum = 0;
	_bRenderingLightingEffect = false;

	if (g_isInRenderLasers || isProjectile)
	{
		_constants.renderType = 2;
	}

	if (scene->D3DInfo != nullptr)
	{
		surface = scene->D3DInfo->ColorMap[0];

		if (scene->D3DInfo->LightMap[0] != nullptr)
		{
			surface2 = scene->D3DInfo->LightMap[0]; // surface2 is a lightmap
			_constants.renderTypeIllum = 1;
		}
	}
	else
	{
		// This is a lighting effect... I wonder which ones? Smoke perhaps?
		_bRenderingLightingEffect = true;
		//log_debug("[DBG] Rendering Lighting Effect");
		const unsigned short ModelIndex_237_1000_0_ResData_LightingEffects = 237;
		L00432750(ModelIndex_237_1000_0_ResData_LightingEffects, 0x02, 0x100);
		XwaSpeciesTMInfo* esi = (XwaSpeciesTMInfo*)s_ExeEnableTable[ModelIndex_237_1000_0_ResData_LightingEffects].pData1;
		surface = (XwaTextureSurface*)esi->pData;
	}

	XwaD3dTextureCacheUpdateOrAdd(surface);

	if (surface2 != nullptr)
	{
		XwaD3dTextureCacheUpdateOrAdd(surface2);
	}

	Direct3DTexture* texture = (Direct3DTexture*)surface->D3dTexture.D3DTextureHandle;
	Direct3DTexture* texture2 = surface2 == nullptr ? nullptr : (Direct3DTexture*)surface2->D3dTexture.D3DTextureHandle;
	_deviceResources->InitPSShaderResourceView(texture->_textureView, texture2 == nullptr ? nullptr : texture2->_textureView.Get());
	_lastTextureSelected = texture;
	_lastLightmapSelected = texture2;
}

void EffectsRenderer::DoStateManagement(const SceneCompData* scene)
{
	// ***************************************************************
	// State management begins here
	// ***************************************************************
	// The following state variables will probably need to be pruned. I suspect we don't
	// need to care about GUI/2D/HUD stuff here anymore.
	// At this point, texture and texture2 have been selected, we can check their names to see if
	// we need to apply effects. If there's a lightmap, it's going to be in texture2.
	// Most of the local flags below should now be class members, but I'll be hand
	_bLastTextureSelectedNotNULL = (_lastTextureSelected != NULL);
	_bLastLightmapSelectedNotNULL = (_lastLightmapSelected != NULL);
	_bIsBlastMark = false;
	_bIsCockpit = false;
	_bIsGunner = false;
	_bIsExplosion = false;
	_bDCIsTransparent = false;
	_bDCElemAlwaysVisible = false;
	_bIsHologram = false;
	_bIsNoisyHolo = false;
	_bWarheadLocked = PlayerDataTable[*g_playerIndex].warheadArmed && PlayerDataTable[*g_playerIndex].warheadLockState == 3;
	_bExternalCamera = PlayerDataTable[*g_playerIndex].Camera.ExternalCamera;
	_bCockpitDisplayed = PlayerDataTable[*g_playerIndex].cockpitDisplayed;
	// If we reach this path, we're no longer rendering the skybox.
	g_bSkyBoxJustFinished = true;

	_bIsTargetHighlighted = false;
	bool bIsGUI = false, bIsLensFlare = false;
	//bool bIsExterior = false, bIsDAT = false;
	//bool bIsActiveCockpit = false,
	//bool bIsDS2CoreExplosion = false;
	bool bIsElectricity = false, bHasMaterial = false;

	if (_bLastTextureSelectedNotNULL) {
		if (g_bDynCockpitEnabled && _lastTextureSelected->is_DynCockpitDst)
		{
			int idx = _lastTextureSelected->DCElementIndex;
			if (idx >= 0 && idx < g_iNumDCElements) {
				_bIsHologram |= g_DCElements[idx].bHologram;
				_bIsNoisyHolo |= g_DCElements[idx].bNoisyHolo;
				_bDCIsTransparent |= g_DCElements[idx].bTransparent;
				_bDCElemAlwaysVisible |= g_DCElements[idx].bAlwaysVisible;
			}
		}

		_bIsLaser = _lastTextureSelected->is_Laser || _lastTextureSelected->is_TurboLaser;
		//_bIsLaser = _constants.renderType == 2;
		g_bIsTargetHighlighted |= _lastTextureSelected->is_HighlightedReticle;
		_bIsTargetHighlighted = g_bIsTargetHighlighted || g_bPrevIsTargetHighlighted;
		if (_bIsTargetHighlighted) g_GameEvent.TargetEvent = TGT_EVT_LASER_LOCK;
		if (PlayerDataTable[*g_playerIndex].warheadArmed) {
			char state = PlayerDataTable[*g_playerIndex].warheadLockState;
			switch (state) {
				// state == 0 warhead armed, no lock
			case 2:
				g_GameEvent.TargetEvent = TGT_EVT_WARHEAD_LOCKING;
				break;
			case 3:
				g_GameEvent.TargetEvent = TGT_EVT_WARHEAD_LOCKED;
				break;
			}
		}
		//bIsLensFlare = lastTextureSelected->is_LensFlare;
		//bIsHyperspaceTunnel = lastTextureSelected->is_HyperspaceAnim;
		_bIsCockpit = _lastTextureSelected->is_CockpitTex;
		_bIsGunner = _lastTextureSelected->is_GunnerTex;
		//bIsExterior = lastTextureSelected->is_Exterior;
		//bIsDAT = lastTextureSelected->is_DAT;
		//bIsActiveCockpit = lastTextureSelected->ActiveCockpitIdx > -1;
		_bIsBlastMark = _lastTextureSelected->is_BlastMark;
		//bIsDS2CoreExplosion = lastTextureSelected->is_DS2_Reactor_Explosion;
		//bIsElectricity = lastTextureSelected->is_Electricity;
		_bHasMaterial = _lastTextureSelected->bHasMaterial;
		_bIsExplosion = _lastTextureSelected->is_Explosion;
		if (_bIsExplosion) g_bExplosionsDisplayedOnCurrentFrame = true;
	}

	//g_bPrevIsPlayerObject = g_bIsPlayerObject;
	//g_bIsPlayerObject = bIsCockpit || bIsExterior || bIsGunner;
	//const bool bIsFloatingGUI = _bLastTextureSelectedNotNULL && _lastTextureSelected->is_Floating_GUI;
	// Hysteresis detection (state is about to switch to render something different, like the HUD)
	g_bPrevIsFloatingGUI3DObject = g_bIsFloating3DObject;
	// Do *not* use g_isInRenderMiniature here, it's not reliable.
	g_bIsFloating3DObject = g_bTargetCompDrawn && _bLastTextureSelectedNotNULL &&
		!_lastTextureSelected->is_Text && !_lastTextureSelected->is_TrianglePointer &&
		!_lastTextureSelected->is_Reticle && !_lastTextureSelected->is_Floating_GUI &&
		!_lastTextureSelected->is_TargetingComp && !bIsLensFlare;

	// The GUI starts rendering whenever we detect a GUI element, or Text, or a bracket.
	// ... or not at all if we're in external view mode with nothing targeted.
	//g_bPrevStartedGUI = g_bStartedGUI;
	// Apr 10, 2020: g_bDisableDiffuse will make the reticle look white when the HUD is
	// hidden. To prevent this, I added bIsAimingHUD to g_bStartedGUI; but I don't know
	// if this breaks VR. If it does, then I need to add !bIsAimingHUD around line 6425,
	// where I'm setting fDisableDiffuse = 1.0f
	//g_bStartedGUI |= bIsGUI || bIsText || bIsBracket || bIsFloatingGUI || bIsReticle;
	// bIsScaleableGUIElem is true when we're about to render a HUD element that can be scaled down with Ctrl+Z
	g_bPrevIsScaleableGUIElem = g_bIsScaleableGUIElem;
	g_bIsScaleableGUIElem = g_bStartedGUI && !g_bIsTrianglePointer && !bIsLensFlare;
}

// Apply specific material properties for the current texture
void EffectsRenderer::ApplyMaterialProperties()
{
	if (!_bHasMaterial || !_bLastTextureSelectedNotNULL)
		return;

	auto &resources = _deviceResources;

	_bModifiedShaders = true;

	if (_lastTextureSelected->material.IsShadeless)
		g_PSCBuffer.fSSAOMaskVal = SHADELESS_MAT;
	else
		g_PSCBuffer.fSSAOMaskVal = _lastTextureSelected->material.Metallic * 0.5f; // Metallicity is encoded in the range 0..0.5 of the SSAOMask
	g_PSCBuffer.fGlossiness = _lastTextureSelected->material.Glossiness;
	g_PSCBuffer.fSpecInt = _lastTextureSelected->material.Intensity;
	g_PSCBuffer.fNMIntensity = _lastTextureSelected->material.NMIntensity;
	g_PSCBuffer.fSpecVal = _lastTextureSelected->material.SpecValue;
	g_PSCBuffer.fAmbient = _lastTextureSelected->material.Ambient;

	if (_lastTextureSelected->material.AlphaToBloom) {
		_bModifiedPixelShader = true;
		_bModifiedShaders = true;
		resources->InitPixelShader(resources->_alphaToBloomPS);
		if (_lastTextureSelected->material.NoColorAlpha)
			g_PSCBuffer.special_control.ExclusiveMask = SPECIAL_CONTROL_NO_COLOR_ALPHA;
		g_PSCBuffer.fBloomStrength = _lastTextureSelected->material.EffectBloom;
	}

	// lastTextureSelected can't be a lightmap anymore, so we don't need (?) to test bIsLightTexture
	if (_lastTextureSelected->material.AlphaIsntGlass /* && !bIsLightTexture */) {
		_bModifiedPixelShader = true;
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = 0.0f;
		resources->InitPixelShader(resources->_noGlassPS);
	}
}

// Apply the SSAO mask/Special materials, like lasers and HUD
void EffectsRenderer::ApplySpecialMaterials()
{
	if (!_bLastTextureSelectedNotNULL)
		return;

	if (g_bIsScaleableGUIElem || /* bIsReticle || bIsText || */ g_bIsTrianglePointer ||
		_lastTextureSelected->is_Debris || _lastTextureSelected->is_GenericSSAOMasked ||
		_lastTextureSelected->is_Electricity || _bIsExplosion ||
		_lastTextureSelected->is_Smoke)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fSSAOMaskVal = SHADELESS_MAT;
		g_PSCBuffer.fGlossiness = DEFAULT_GLOSSINESS;
		g_PSCBuffer.fSpecInt = DEFAULT_SPEC_INT;
		g_PSCBuffer.fNMIntensity = 0.0f;
		g_PSCBuffer.fSpecVal = 0.0f;
		g_PSCBuffer.bIsShadeless = 1;

		g_PSCBuffer.fPosNormalAlpha = 0.0f;
	}
	else if (_lastTextureSelected->is_Debris || _lastTextureSelected->is_Trail ||
		_lastTextureSelected->is_CockpitSpark || _lastTextureSelected->is_Spark ||
		_lastTextureSelected->is_Chaff || _lastTextureSelected->is_Missile
		)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fSSAOMaskVal = PLASTIC_MAT;
		g_PSCBuffer.fGlossiness = DEFAULT_GLOSSINESS;
		g_PSCBuffer.fSpecInt = DEFAULT_SPEC_INT;
		g_PSCBuffer.fNMIntensity = 0.0f;
		g_PSCBuffer.fSpecVal = 0.0f;

		g_PSCBuffer.fPosNormalAlpha = 0.0f;
	}
	else if (_lastTextureSelected->is_Laser) {
		_bModifiedShaders = true;
		g_PSCBuffer.fSSAOMaskVal = EMISSION_MAT;
		g_PSCBuffer.fGlossiness = DEFAULT_GLOSSINESS;
		g_PSCBuffer.fSpecInt = DEFAULT_SPEC_INT;
		g_PSCBuffer.fNMIntensity = 0.0f;
		g_PSCBuffer.fSpecVal = 0.0f;
		g_PSCBuffer.bIsShadeless = 1;

		g_PSCBuffer.fPosNormalAlpha = 0.0f;
	}
}

void EffectsRenderer::ApplyDiegeticCockpit()
{
	// g_OPTMeshTransformCB.MeshTransform should be reset to identity for each mesh at
	// the beginning of MainSceneHook(). So if we just return from this function, no
	// transform will be applied to the current mesh
	if (!g_bDiegeticCockpitEnabled || !_bHasMaterial || !_bLastTextureSelectedNotNULL)
		return;

	auto &resources = _deviceResources;

	auto GetThrottle = []() {
		CraftInstance *craftInstance = GetPlayerCraftInstanceSafe();
		if (craftInstance == NULL) return 0.0f;
		return craftInstance->EngineThrottleInput / 65535.0f;
	};

	auto GetHyperThrottle = []() {
		float timeInHyperspace = (float)PlayerDataTable[*g_playerIndex].timeInHyperspace;
		switch (g_HyperspacePhaseFSM) {
		case HS_INIT_ST:
			return 0.0f;
		case HS_HYPER_ENTER_ST:
			return min(1.0f, timeInHyperspace / 550.0f);
		case HS_HYPER_TUNNEL_ST:
			return 1.0f;
		case HS_HYPER_EXIT_ST:
			return max(0.0f, 1.0f - timeInHyperspace / 190.0f);
		}
		return 0.0f;
	};

	DiegeticMeshType DiegeticMesh = _lastTextureSelected->material.DiegeticMesh;
	switch (DiegeticMesh) {
	case DM_JOYSTICK: {
		// _bJoystickTransformReady is set to false at the beginning of each frame. We set it to
		// true as soon as one mesh computes it, so that we don't compute it several times per
		// frame.
		if (!_bJoystickTransformReady && g_pSharedDataJoystick != NULL && g_SharedMemJoystick.IsDataReady()) {
			Vector3 JoystickRoot = _lastTextureSelected->material.JoystickRoot;
			float MaxYaw = _lastTextureSelected->material.JoystickMaxYaw;
			float MaxPitch = _lastTextureSelected->material.JoystickMaxPitch;
			Matrix4 T, R;

			// Translate the JoystickRoot to the origin
			T.identity(); T.translate(-JoystickRoot);
			_joystickMeshTransform = T;

			R.identity(); R.rotateY(MaxYaw * g_pSharedDataJoystick->JoystickYaw);
			_joystickMeshTransform = R * _joystickMeshTransform;

			R.identity(); R.rotateX(MaxPitch * g_pSharedDataJoystick->JoystickPitch);
			_joystickMeshTransform = R * _joystickMeshTransform;

			// Return the system to its original position
			T.identity(); T.translate(JoystickRoot);
			_joystickMeshTransform = T * _joystickMeshTransform;

			// We need to transpose the matrix because the Vertex Shader is expecting the
			// matrix in this format
			_joystickMeshTransform.transpose();
			// Avoid computing the transform above more than once per frame:
			_bJoystickTransformReady = true;
		}
		g_OPTMeshTransformCB.MeshTransform = _joystickMeshTransform;
		break;
	}
	case DM_THR_ROT_X:
	case DM_THR_ROT_Y:
	case DM_THR_ROT_Z:
	case DM_HYPER_ROT_X:
	case DM_HYPER_ROT_Y:
	case DM_HYPER_ROT_Z:
	{
		// Caching the transform may not work well when both the throttle and the hyper
		// throttle are present.
		//if (!_bThrottleTransformReady)
		{
			float throttle = (DiegeticMesh == DM_THR_ROT_X || DiegeticMesh == DM_THR_ROT_Y || DiegeticMesh == DM_THR_ROT_Z) ?
				GetThrottle() : GetHyperThrottle();

			// Build the transform matrix
			Vector3 ThrottleRoot = _lastTextureSelected->material.ThrottleRoot;
			float ThrottleMaxAngle = _lastTextureSelected->material.ThrottleMaxAngle;
			Matrix4 T, R;

			// Translate the root to the origin
			T.identity(); T.translate(-ThrottleRoot);
			_throttleMeshTransform = T;

			// Apply the rotation
			R.identity();
			if (DiegeticMesh == DM_THR_ROT_X || DiegeticMesh == DM_HYPER_ROT_X) R.rotateX(throttle * ThrottleMaxAngle);
			if (DiegeticMesh == DM_THR_ROT_Y || DiegeticMesh == DM_HYPER_ROT_Y) R.rotateY(throttle * ThrottleMaxAngle);
			if (DiegeticMesh == DM_THR_ROT_Z || DiegeticMesh == DM_HYPER_ROT_Z) R.rotateZ(throttle * ThrottleMaxAngle);
			_throttleMeshTransform = R * _throttleMeshTransform;

			// Return the system to its original position
			T.identity(); T.translate(ThrottleRoot);
			_throttleMeshTransform = T * _throttleMeshTransform;

			// We need to transpose the matrix because the Vertex Shader is expecting the
			// matrix in this format
			_throttleMeshTransform.transpose();
			// Avoid computing the transform above more than once per frame:
			//_bThrottleTransformReady = true;
		}
		g_OPTMeshTransformCB.MeshTransform = _throttleMeshTransform;
		break;
	}
	case DM_THR_TRANS:
	case DM_HYPER_TRANS:
		// Caching the transform may not work well when both the throttle and the hyper
		// throttle are present.
		//if (!_bThrottleTransformReady)
		{
			float throttle = DiegeticMesh == DM_THR_TRANS ? GetThrottle() : GetHyperThrottle();

			// Build the transform matrix
			Matrix4 T, R;
			Vector3 ThrottleStart = _lastTextureSelected->material.ThrottleStart;
			Vector3 ThrottleEnd = _lastTextureSelected->material.ThrottleEnd;
			Vector3 Dir = ThrottleEnd - ThrottleStart;
			Dir.normalize();

			// Apply the rotation
			Dir *= throttle;
			T.translate(Dir);
			_throttleMeshTransform = T;

			// We need to transpose the matrix because the Vertex Shader is expecting the
			// matrix in this format
			_throttleMeshTransform.transpose();
			// Avoid computing the transform above more than once per frame:
			//_bThrottleTransformReady = true;
		}
		g_OPTMeshTransformCB.MeshTransform = _throttleMeshTransform;
		break;
	case DM_THR_ROT_ANY:
	case DM_HYPER_ROT_ANY:
		// Caching the transform may not work well when both the throttle and the hyper
		// throttle are present.
		//if (!_bThrottleRotAxisToZPlusReady)
		{
			float throttle = DiegeticMesh == DM_THR_ROT_ANY ? GetThrottle() : GetHyperThrottle();
			Material *material = &(_lastTextureSelected->material);

			float ThrottleMaxAngle = material->ThrottleMaxAngle;
			Vector3 ThrottleStart = material->ThrottleStart;
			Vector3 ThrottleEnd = material->ThrottleEnd;
			if (!material->bRotAxisToZPlusReady) {
				// Cache RotAxisToZPlus so that we don't have to compute it on every frame
				Vector4 Dir;
				Dir.x = ThrottleEnd.x - ThrottleStart.x;
				Dir.y = ThrottleEnd.y - ThrottleStart.y;
				Dir.z = ThrottleEnd.z - ThrottleStart.z;
				Dir.w = 0;
				material->RotAxisToZPlus = GetSimpleDirectionMatrix(Dir, false);
				material->bRotAxisToZPlusReady = true;
			}
			Matrix4 RotAxisToZPlus = material->RotAxisToZPlus;

			Matrix4 T, R;
			// Translate the root to the origin
			T.identity(); T.translate(-ThrottleStart);
			_throttleMeshTransform = T;

			// Align with Z+
			_throttleMeshTransform = RotAxisToZPlus * _throttleMeshTransform;

			// Apply the rotation
			R.identity();
			R.rotateZ(throttle * ThrottleMaxAngle);
			_throttleMeshTransform = R * _throttleMeshTransform;

			// Invert Z+ alignment
			RotAxisToZPlus.transpose();
			_throttleMeshTransform = RotAxisToZPlus * _throttleMeshTransform;

			// Return the system to its original position
			T.identity(); T.translate(ThrottleStart);
			_throttleMeshTransform = T * _throttleMeshTransform;

			// We need to transpose the matrix because the Vertex Shader is expecting the
			// matrix in this format
			_throttleMeshTransform.transpose();
			//_bThrottleRotAxisToZPlusReady = true;
		}
		g_OPTMeshTransformCB.MeshTransform = _throttleMeshTransform;
		break;
	}
}

void EffectsRenderer::ApplyMeshTransform()
{
	// g_OPTMeshTransformCB.MeshTransform should be reset to identity for each mesh at
	// the beginning of MainSceneHook(). So if we just return from this function, no
	// transform will be applied to the current mesh
	if (!_bHasMaterial || !_bLastTextureSelectedNotNULL || !_lastTextureSelected->material.meshTransform.bDoTransform)
		return;

	auto &resources = _deviceResources;
	Material *material = &(_lastTextureSelected->material);
	
	material->meshTransform.UpdateTransform();
	g_OPTMeshTransformCB.MeshTransform = material->meshTransform.ComputeTransform();
}

// Apply BLOOM flags and 32-bit mode enhancements
void EffectsRenderer::ApplyBloomSettings()
{
	if (!_bLastTextureSelectedNotNULL)
		return;

	if (_bIsLaser) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = _lastTextureSelected->is_Laser ?
			g_BloomConfig.fLasersStrength : g_BloomConfig.fTurboLasersStrength;
		g_PSCBuffer.bIsLaser = g_config.EnhanceLasers ? 2 : 1;
	}
	// Send the flag for light textures (enhance them in 32-bit mode, apply bloom)
	// TODO: Check if the animation for light textures still works
	else if (_bLastLightmapSelectedNotNULL) {
		_bModifiedShaders = true;
		int anim_idx = _lastTextureSelected->material.GetCurrentATCIndex(NULL, LIGHTMAP_ATC_IDX);
		g_PSCBuffer.fBloomStrength = _lastTextureSelected->is_CockpitTex ?
			g_BloomConfig.fCockpitStrength : g_BloomConfig.fLightMapsStrength;
		// If this is an animated light map, then use the right intensity setting
		// TODO: Make the following code more efficient
		if (anim_idx > -1) {
			AnimatedTexControl *atc = &(g_AnimatedMaterials[anim_idx]);
			g_PSCBuffer.fBloomStrength = atc->Sequence[atc->AnimIdx].intensity;
		}
		g_PSCBuffer.bIsLightTexture = g_config.EnhanceIllumination ? 2 : 1;
	}
	// Set the flag for EngineGlow and Explosions (enhance them in 32-bit mode, apply bloom)
	else if (_lastTextureSelected->is_EngineGlow) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fEngineGlowStrength;
		g_PSCBuffer.bIsEngineGlow = g_config.EnhanceEngineGlow ? 2 : 1;
	}
	else if (_lastTextureSelected->is_Electricity || _bIsExplosion)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fExplosionsStrength;
		g_PSCBuffer.bIsEngineGlow = g_config.EnhanceExplosions ? 2 : 1;
	}
	else if (_lastTextureSelected->is_LensFlare) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fLensFlareStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	/*
	// I believe Suns are not rendered here
	else if (bIsSun) {
		bModifiedShaders = true;
		// If there's a 3D sun in the scene, then we shouldn't apply bloom to Sun textures  they should be invisible
		g_PSCBuffer.fBloomStrength = g_b3DSunPresent ? 0.0f : g_BloomConfig.fSunsStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	} */
	else if (_lastTextureSelected->is_Spark) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fSparksStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	else if (_lastTextureSelected->is_CockpitSpark) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fCockpitSparksStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	else if (_lastTextureSelected->is_Chaff)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fSparksStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	else if (_lastTextureSelected->is_Missile)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fMissileStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	else if (_lastTextureSelected->is_SkydomeLight) {
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = g_BloomConfig.fSkydomeLightStrength;
		g_PSCBuffer.bIsEngineGlow = 1;
	}
	else if (!_bLastLightmapSelectedNotNULL && _lastTextureSelected->material.GetCurrentATCIndex(NULL) > -1) {
		_bModifiedShaders = true;
		// TODO: Check if this animation still works
		int anim_idx = _lastTextureSelected->material.GetCurrentATCIndex(NULL);
		// If this is an animated light map, then use the right intensity setting
		// TODO: Make the following code more efficient
		if (anim_idx > -1) {
			AnimatedTexControl *atc = &(g_AnimatedMaterials[anim_idx]);
			g_PSCBuffer.fBloomStrength = atc->Sequence[atc->AnimIdx].intensity;
		}
	}

	// Remove Bloom for all textures with materials tagged as "NoBloom"
	if (_bHasMaterial && _lastTextureSelected->material.NoBloom)
	{
		_bModifiedShaders = true;
		g_PSCBuffer.fBloomStrength = 0.0f;
		g_PSCBuffer.bIsEngineGlow = 0;
	}
}

void EffectsRenderer::ExtraPreprocessing()
{
	// Extra processing before the draw call. VR-specific stuff, for instance
}

void EffectsRenderer::AddLaserLights(const SceneCompData* scene)
{
	XwaVector3 *MeshVertices = scene->MeshVertices;
	int MeshVerticesCount = *(int*)((int)scene->MeshVertices - 8);
	XwaTextureVertex *MeshTextureVertices = scene->MeshTextureVertices;
	int MeshTextureVerticesCount = *(int*)((int)scene->MeshTextureVertices - 8);
	Vector4 tempv0, tempv1, tempv2, P;
	Vector2 UV0, UV1, UV2, UV = _lastTextureSelected->material.LightUVCoordPos;
	Matrix4 T = XwaTransformToMatrix4(scene->WorldViewTransform);

	if (g_bDumpSSAOBuffers && bD3DDumpOBJEnabled) {
		// This is a new mesh, dump all the vertices.
		//log_debug("[DBG] Writting obj_idx: %d, MeshVerticesCount: %d, TexCount: %d, FacesCount: %d",
		//	D3DOBJLaserGroup, MeshVerticesCount, MeshTextureVerticesCount, scene->FacesCount);
		fprintf(D3DDumpLaserOBJFile, "o obj-%d\n", D3DOBJLaserGroup);

		Matrix4 T = XwaTransformToMatrix4(scene->WorldViewTransform);
		for (int i = 0; i < MeshVerticesCount; i++) {
			XwaVector3 v = MeshVertices[i];
			Vector4 V(v.x, v.y, v.z, 1.0f);
			V = T * V;
			// OPT to meters conversion:
			V *= OPT_TO_METERS;
			fprintf(D3DDumpLaserOBJFile, "v %0.6f %0.6f %0.6f\n", V.x, V.y, V.z);
		}
		fprintf(D3DDumpLaserOBJFile, "\n");

		for (int i = 0; i < MeshTextureVerticesCount; i++) {
			XwaTextureVertex vt = MeshTextureVertices[i];
			fprintf(D3DDumpLaserOBJFile, "vt %0.3f %0.3f\n", vt.u, vt.v);
		}
		fprintf(D3DDumpLaserOBJFile, "\n");

		D3DOBJLaserGroup++;
	}

	// Here we look at the uv's of each face and see if the current triangle contains
	// the uv coord we're looking for (the default is (0.1, 0.5)). If the uv is contained,
	// then we compute the 3D point using its barycentric coords and add it to the list
	for (int faceIndex = 0; faceIndex < scene->FacesCount; faceIndex++)
	{
		OptFaceDataNode_01_Data_Indices& faceData = scene->FaceIndices[faceIndex];
		int edgesCount = faceData.Edge[3] == -1 ? 3 : 4;
		// This converts quads into 2 tris if necessary
		for (int edge = 2; edge < edgesCount; edge++)
		{
			D3dTriangle t;
			t.v1 = 0;
			t.v2 = edge - 1;
			t.v3 = edge;

			UV0 = XwaTextureVertexToVector2(MeshTextureVertices[faceData.TextureVertex[t.v1]]);
			UV1 = XwaTextureVertexToVector2(MeshTextureVertices[faceData.TextureVertex[t.v2]]);
			UV2 = XwaTextureVertexToVector2(MeshTextureVertices[faceData.TextureVertex[t.v3]]);
			tempv0 = OPT_TO_METERS * T * XwaVector3ToVector4(MeshVertices[faceData.Vertex[t.v1]]);
			tempv1 = OPT_TO_METERS * T * XwaVector3ToVector4(MeshVertices[faceData.Vertex[t.v2]]);
			tempv2 = OPT_TO_METERS * T * XwaVector3ToVector4(MeshVertices[faceData.Vertex[t.v3]]);

			if (g_bDumpSSAOBuffers && bD3DDumpOBJEnabled) {
				fprintf(D3DDumpLaserOBJFile, "f %d/%d %d/%d %d/%d\n",
					faceData.Vertex[t.v1] + D3DTotalLaserVertices, faceData.TextureVertex[t.v1] + D3DTotalLaserTextureVertices,
					faceData.Vertex[t.v2] + D3DTotalLaserVertices, faceData.TextureVertex[t.v2] + D3DTotalLaserTextureVertices,
					faceData.Vertex[t.v3] + D3DTotalLaserVertices, faceData.TextureVertex[t.v3] + D3DTotalLaserTextureVertices);
			}

			// Our coordinate system has the Y-axis inverted. See also XwaD3dVertexShader
			tempv0.y = -tempv0.y;
			tempv1.y = -tempv1.y;
			tempv2.y = -tempv2.y;

			float u, v;
			if (IsInsideTriangle(UV, UV0, UV1, UV2, &u, &v)) {
				P = tempv0 + u * (tempv2 - tempv0) + v * (tempv1 - tempv0);
				// Compute the normal for this face, we'll need that for directional lights
				Vector3 e1 = Vector3(tempv1.x - tempv0.x,
									 tempv1.y - tempv0.y,
									 tempv1.z - tempv0.z);

				Vector3 e2 = Vector3(tempv2.x - tempv1.x,
									 tempv2.y - tempv1.y,
									 tempv2.z - tempv1.z);
				Vector3 N = e1.cross(e2);
				// We need to invert the Z component or the lights won't work properly.
				N.z = -N.z;
				N.normalize();

				// Prevent lasers behind the camera: they will cause a very bright flash
				if (P.z > 0.01)
					g_LaserList.insert(Vector3(P.x, P.y, P.z),
						_lastTextureSelected->material.Light,
						N,
						_lastTextureSelected->material.LightFalloff,
						_lastTextureSelected->material.LightAngle);
				//log_debug("[DBG] LaserLight: %0.1f, %0.1f, %0.1f", P.x, P.y, P.z);
			}
		}
	}

	if (g_bDumpSSAOBuffers && bD3DDumpOBJEnabled) {
		D3DTotalLaserVertices += MeshVerticesCount;
		D3DTotalLaserTextureVertices += MeshTextureVerticesCount;
		fprintf(D3DDumpLaserOBJFile, "\n");
	}
}

void EffectsRenderer::ApplyProceduralLava()
{
	static float iTime = 0.0f;
	auto &context = _deviceResources->_d3dDeviceContext;
	iTime = g_HiResTimer.global_time_s * _lastTextureSelected->material.LavaSpeed;

	_bModifiedShaders = true;
	_bModifiedPixelShader = true;
	_bModifiedSamplerState = true;

	g_ShadertoyBuffer.iTime = iTime;
	g_ShadertoyBuffer.Style = _lastTextureSelected->material.LavaTiling;
	g_ShadertoyBuffer.iResolution[0] = _lastTextureSelected->material.LavaSize;
	g_ShadertoyBuffer.iResolution[1] = _lastTextureSelected->material.EffectBloom;
	// SunColor[0] --> Color
	g_ShadertoyBuffer.SunColor[0].x = _lastTextureSelected->material.LavaColor.x;
	g_ShadertoyBuffer.SunColor[0].y = _lastTextureSelected->material.LavaColor.y;
	g_ShadertoyBuffer.SunColor[0].z = _lastTextureSelected->material.LavaColor.z;
	/*
	// SunColor[1] --> LavaNormalMult
	g_ShadertoyBuffer.SunColor[1].x = lastTextureSelected->material.LavaNormalMult.x;
	g_ShadertoyBuffer.SunColor[1].y = lastTextureSelected->material.LavaNormalMult.y;
	g_ShadertoyBuffer.SunColor[1].z = lastTextureSelected->material.LavaNormalMult.z;
	// SunColor[2] --> LavaPosMult
	g_ShadertoyBuffer.SunColor[2].x = lastTextureSelected->material.LavaPosMult.x;
	g_ShadertoyBuffer.SunColor[2].y = lastTextureSelected->material.LavaPosMult.y;
	g_ShadertoyBuffer.SunColor[2].z = lastTextureSelected->material.LavaPosMult.z;

	g_ShadertoyBuffer.bDisneyStyle = lastTextureSelected->material.LavaTranspose;
	*/

	_deviceResources->InitPixelShader(_deviceResources->_lavaPS);
	// Set the noise texture and sampler state with wrap/repeat enabled.
	context->PSSetShaderResources(1, 1, _deviceResources->_grayNoiseSRV.GetAddressOf());
	// bModifiedSamplerState restores this sampler state at the end of this instruction.
	context->PSSetSamplers(1, 1, _deviceResources->_repeatSamplerState.GetAddressOf());

	// Set the constant buffer
	_deviceResources->InitPSConstantBufferHyperspace(
		_deviceResources->_hyperspaceConstantBuffer.GetAddressOf(), &g_ShadertoyBuffer);
}

void EffectsRenderer::ApplyGreebles()
{
	if (!g_bAutoGreeblesEnabled || !_bHasMaterial || _lastTextureSelected->material.GreebleDataIdx == -1)
		return;

	auto &resources = _deviceResources;
	auto &context = _deviceResources->_d3dDeviceContext;
	Material *material = &(_lastTextureSelected->material);
	GreebleData *greeble_data = &(g_GreebleData[material->GreebleDataIdx]);

	bool bIsRegularGreeble = (!_bLastLightmapSelectedNotNULL && greeble_data->GreebleTexIndex[0] != -1);
	bool bIsLightmapGreeble = (_bLastLightmapSelectedNotNULL && greeble_data->GreebleLightMapIndex[0] != -1);
	if (bIsRegularGreeble || bIsLightmapGreeble) {
		// 0x1: This greeble will use normal mapping
		// 0x2: This is a lightmap greeble
		// See PixelShaderGreeble for a full list of bits used in this effect
		uint32_t GreebleControlBits = bIsLightmapGreeble ? 0x2 : 0x0;
		_bModifiedShaders = true;
		_bModifiedPixelShader = true;

		resources->InitPixelShader(resources->_pixelShaderGreeble);
		if (bIsRegularGreeble) {
			g_PSCBuffer.GreebleMix1 = greeble_data->GreebleMix[0];
			g_PSCBuffer.GreebleMix2 = greeble_data->GreebleMix[1];

			g_PSCBuffer.GreebleDist1 = greeble_data->GreebleDist[0];
			g_PSCBuffer.GreebleDist2 = greeble_data->GreebleDist[1];

			g_PSCBuffer.GreebleScale1 = greeble_data->GreebleScale[0];
			g_PSCBuffer.GreebleScale2 = greeble_data->GreebleScale[1];

			uint32_t blendMask1 = greeble_data->GreebleTexIndex[0] != -1 ? greeble_data->greebleBlendMode[0] : 0x0;
			uint32_t blendMask2 = greeble_data->GreebleTexIndex[1] != -1 ? greeble_data->greebleBlendMode[1] : 0x0;
			if (blendMask1 == GBM_NORMAL_MAP || blendMask1 == GBM_UV_DISP_AND_NORMAL_MAP ||
				blendMask2 == GBM_NORMAL_MAP || blendMask2 == GBM_UV_DISP_AND_NORMAL_MAP)
				GreebleControlBits |= 0x1;
			if (blendMask1 == GBM_UV_DISP || blendMask1 == GBM_UV_DISP_AND_NORMAL_MAP ||
				blendMask2 == GBM_UV_DISP || blendMask2 == GBM_UV_DISP_AND_NORMAL_MAP)
				g_PSCBuffer.UVDispMapResolution = greeble_data->UVDispMapResolution;
			g_PSCBuffer.GreebleControl = (GreebleControlBits << 16) | (blendMask2 << 4) | blendMask1;

			// Load regular greebles...
			if (greeble_data->GreebleTexIndex[0] != -1)
				context->PSSetShaderResources(10, 1, &(resources->_extraTextures[greeble_data->GreebleTexIndex[0]]));
			if (greeble_data->GreebleTexIndex[1] != -1)
				context->PSSetShaderResources(11, 1, &(resources->_extraTextures[greeble_data->GreebleTexIndex[1]]));
		}
		else if (bIsLightmapGreeble) {
			g_PSCBuffer.GreebleMix1 = greeble_data->GreebleLightMapMix[0];
			g_PSCBuffer.GreebleMix2 = greeble_data->GreebleLightMapMix[1];

			g_PSCBuffer.GreebleDist1 = greeble_data->GreebleLightMapDist[0];
			g_PSCBuffer.GreebleDist2 = greeble_data->GreebleLightMapDist[1];

			g_PSCBuffer.GreebleScale1 = greeble_data->GreebleLightMapScale[0];
			g_PSCBuffer.GreebleScale2 = greeble_data->GreebleLightMapScale[1];

			uint32_t blendMask1 = greeble_data->GreebleLightMapIndex[0] != -1 ? greeble_data->greebleLightMapBlendMode[0] : 0x0;
			uint32_t blendMask2 = greeble_data->GreebleLightMapIndex[1] != -1 ? greeble_data->greebleLightMapBlendMode[1] : 0x0;
			if (blendMask1 == GBM_NORMAL_MAP || blendMask1 == GBM_UV_DISP_AND_NORMAL_MAP ||
				blendMask2 == GBM_NORMAL_MAP || blendMask2 == GBM_UV_DISP_AND_NORMAL_MAP)
				GreebleControlBits |= 0x1;
			if (blendMask1 == GBM_UV_DISP || blendMask1 == GBM_UV_DISP_AND_NORMAL_MAP ||
				blendMask2 == GBM_UV_DISP || blendMask2 == GBM_UV_DISP_AND_NORMAL_MAP)
				g_PSCBuffer.UVDispMapResolution = greeble_data->UVDispMapResolution;
			g_PSCBuffer.GreebleControl = (GreebleControlBits << 16) | (blendMask2 << 4) | blendMask1;

			// ... or load lightmap greebles
			if (greeble_data->GreebleLightMapIndex[0] != -1)
				context->PSSetShaderResources(10, 1, &(resources->_extraTextures[greeble_data->GreebleLightMapIndex[0]]));
			if (greeble_data->GreebleLightMapIndex[1] != -1)
				context->PSSetShaderResources(11, 1, &(resources->_extraTextures[greeble_data->GreebleLightMapIndex[1]]));
		}
	}
}

void EffectsRenderer::ApplyAnimatedTextures(int objectId, bool bInstanceEvent)
{
	// Do not apply animations if there's no material or there's a greeble in the current
	// texture
	if (!_bHasMaterial || _lastTextureSelected->material.GreebleDataIdx != -1)
		return;

	bool bIsDCDamageTex = false;
	std::vector<int> TexATCIndices, LightATCIndices;
	InstanceEvent* instEvent = nullptr;
	uint32_t OverlayCtrl = 0;

	if (bInstanceEvent) {
		// This is an instance ATC
		instEvent = ObjectIDToInstanceEvent(objectId, _lastTextureSelected->material.Id);
		if (instEvent != nullptr) {
			TexATCIndices = _lastTextureSelected->material.GetCurrentInstATCIndex(objectId, *instEvent, TEXTURE_ATC_IDX);
			LightATCIndices = _lastTextureSelected->material.GetCurrentInstATCIndex(objectId, *instEvent, LIGHTMAP_ATC_IDX);
		}
	}
	else {
		// This is a global ATC
		int TexATCIndex = _lastTextureSelected->material.GetCurrentATCIndex(&bIsDCDamageTex, TEXTURE_ATC_IDX);
		int LightATCIndex = _lastTextureSelected->material.GetCurrentATCIndex(NULL, LIGHTMAP_ATC_IDX);
		if (TexATCIndex != -1) TexATCIndices.push_back(TexATCIndex);
		if (LightATCIndex != -1) LightATCIndices.push_back(LightATCIndex);
	}
	
	// If there's no texture animation and no lightmap animation, then there's nothing to do here
	if (TexATCIndices.size() == 0 && LightATCIndices.size() == 0)
		return;

	auto &resources = _deviceResources;
	auto &context = _deviceResources->_d3dDeviceContext;
	const bool bRenderingDC = g_PSCBuffer.DynCockpitSlots > 0;

	_bModifiedShaders = true;
	_bModifiedPixelShader = true;

	// If we reach this point then one of LightMapATCIndex or TextureATCIndex must be > -1 or both!
	// If we're rendering a DC element, we don't want to replace the shader
	if (!bRenderingDC)
		resources->InitPixelShader(resources->_pixelShaderAnim);

	// We're not updating the Hyperspace FSM in the D3DRendererHook, we still do it in
	// Direct3DDevice::Execute. That means that we may reach this point without knowing
	// we've entered hyperspace. Let's provide a quick update here:
	g_PSCBuffer.bInHyperspace = PlayerDataTable[*g_playerIndex].hyperspacePhase != 0 || g_HyperspacePhaseFSM != HS_INIT_ST;
	g_PSCBuffer.AuxColor.x = 1.0f;
	g_PSCBuffer.AuxColor.y = 1.0f;
	g_PSCBuffer.AuxColor.z = 1.0f;
	g_PSCBuffer.AuxColor.w = 1.0f;

	g_PSCBuffer.AuxColorLight.x = 1.0f;
	g_PSCBuffer.AuxColorLight.y = 1.0f;
	g_PSCBuffer.AuxColorLight.z = 1.0f;
	g_PSCBuffer.AuxColorLight.w = 1.0f;

	int extraTexIdx = -1, extraLightIdx = -1;
	for (int TexATCIndex : TexATCIndices) {
		AnimatedTexControl* atc = bInstanceEvent ?
			&(g_AnimatedInstMaterials[TexATCIndex]) : &(g_AnimatedMaterials[TexATCIndex]);
		int idx = atc->AnimIdx;
		extraTexIdx = atc->Sequence[idx].ExtraTextureIndex;
		if (atc->BlackToAlpha)
			g_PSCBuffer.special_control.ExclusiveMask = SPECIAL_CONTROL_BLACK_TO_ALPHA;
		else if (atc->AlphaIsBloomMask)
			g_PSCBuffer.special_control.ExclusiveMask = SPECIAL_CONTROL_ALPHA_IS_BLOOM_MASK;
		else
			g_PSCBuffer.special_control.ExclusiveMask = 0;
		g_PSCBuffer.AuxColor = atc->Tint;
		g_PSCBuffer.Offset = atc->Offset;
		g_PSCBuffer.AspectRatio = atc->AspectRatio;
		g_PSCBuffer.Clamp = atc->Clamp;
		g_PSCBuffer.fBloomStrength = atc->Sequence[idx].intensity;

		if (extraTexIdx > -1) {
			if (atc->OverlayCtrl == 0) {
				// Use the following when using std::vector<ID3D11ShaderResourceView*>:
				// We cannot use InitPSShaderResourceView here because that will set slots 0 and 1, thus changing
				// the DC foreground SRV
				context->PSSetShaderResources(0, 1, &(resources->_extraTextures[extraTexIdx]));
			}
			if ((atc->OverlayCtrl & OVERLAY_CTRL_MULT) != 0x0) {
				OverlayCtrl |= OVERLAY_CTRL_MULT;
				g_PSCBuffer.OverlayCtrl = OverlayCtrl;
				// Set the animated texture in the multiplier layer
				context->PSSetShaderResources(14, 1, &(resources->_extraTextures[extraTexIdx]));
			}
			if ((atc->OverlayCtrl & OVERLAY_CTRL_SCREEN) != 0x0) {
				OverlayCtrl |= OVERLAY_CTRL_SCREEN;
				g_PSCBuffer.OverlayCtrl = OverlayCtrl;
				// Set the animated texture in the screen layer
				context->PSSetShaderResources(15, 1, &(resources->_extraTextures[extraTexIdx]));
			}

			// Force the use of damage textures if DC is on. This makes damage textures visible
			// even when no cover texture is available:
			if (bRenderingDC)
				g_DCPSCBuffer.use_damage_texture = bIsDCDamageTex;
		}
	}

	for (int LightATCIndex : LightATCIndices) {
		AnimatedTexControl *atc = bInstanceEvent ?
			&(g_AnimatedInstMaterials[LightATCIndex]) : &(g_AnimatedMaterials[LightATCIndex]);
		int idx = atc->AnimIdx;
		extraLightIdx = atc->Sequence[idx].ExtraTextureIndex;
		if (atc->BlackToAlpha)
			g_PSCBuffer.special_control_light.ExclusiveMask = SPECIAL_CONTROL_BLACK_TO_ALPHA;
		else if (atc->AlphaIsBloomMask)
			g_PSCBuffer.special_control_light.ExclusiveMask = SPECIAL_CONTROL_ALPHA_IS_BLOOM_MASK;
		else
			g_PSCBuffer.special_control_light.ExclusiveMask = 0;
		g_PSCBuffer.AuxColorLight = atc->Tint;
		// TODO: We might need two of these settings below, one for the regular tex and one for the lightmap
		g_PSCBuffer.Offset = atc->Offset;
		g_PSCBuffer.AspectRatio = atc->AspectRatio;
		g_PSCBuffer.Clamp = atc->Clamp;
		g_PSCBuffer.fBloomStrength = atc->Sequence[idx].intensity;

		// Set the animated lightmap in slot 1, but only if we're not rendering DC -- DC uses
		// that slot for something else
		if (extraLightIdx > -1 && !bRenderingDC) {
			if (atc->OverlayCtrl == 0) {
				// Use the following when using std::vector<ID3D11ShaderResourceView*>:
				context->PSSetShaderResources(1, 1, &(resources->_extraTextures[extraLightIdx]));
			}
			if ((atc->OverlayCtrl & OVERLAY_ILLUM_CTRL_MULT) != 0x0) {
				OverlayCtrl |= OVERLAY_ILLUM_CTRL_MULT;
				g_PSCBuffer.OverlayCtrl = OverlayCtrl;
				// Set the animated texture in the multiplier layer
				context->PSSetShaderResources(14, 1, &(resources->_extraTextures[extraLightIdx]));
			}
				if ((atc->OverlayCtrl & OVERLAY_ILLUM_CTRL_SCREEN) != 0x0) {
					OverlayCtrl |= OVERLAY_ILLUM_CTRL_SCREEN;
					g_PSCBuffer.OverlayCtrl = OverlayCtrl;
				// Set the animated texture in the screen layer
				context->PSSetShaderResources(15, 1, &(resources->_extraTextures[extraLightIdx]));
			}
		}
	}
}

void EffectsRenderer::ApplyNormalMapping()
{
	if (!g_bFNEnable || !_bHasMaterial || !_lastTextureSelected->material.NormalMapLoaded ||
		_lastTextureSelected->NormalMapIdx == -1 || _lastMeshVertexTangentsView == nullptr)
		return;

	auto &resources = _deviceResources;
	auto &context = _deviceResources->_d3dDeviceContext;
	Material *material = &(_lastTextureSelected->material);
	_bModifiedShaders = true;

	// Enable normal mapping and make sure the proper intensity is set
	g_PSCBuffer.bDoNormalMapping = 1;
	g_PSCBuffer.fNMIntensity = _lastTextureSelected->material.NMIntensity;
	// Set the normal map
	context->PSSetShaderResources(13, 1, &(resources->_extraTextures[_lastTextureSelected->NormalMapIdx]));
}

void EffectsRenderer::ApplyRTShadows() {
	
	{
		_bModifiedShaders = true;
		// Enable/Disable Raytracing as necessary
		g_PSCBuffer.bDoRaytracing = g_bRTEnabled && (lbvh != nullptr);
	}

	if (!g_bRTEnabled || lbvh == nullptr)
		return;

	auto &context = _deviceResources->_d3dDeviceContext;

	Matrix4 RTScale = Matrix4().scale(1.0f / lbvh->scale);
	Matrix4 transformWorldViewInv = RTScale * _constants.transformWorldView;
	transformWorldViewInv = transformWorldViewInv.invert();
	// Invert transformWorldView and send it to the ray-tracer
	//g_RTConstantsBuffer.TransformWorldViewInv = transformWorldViewInv;
	//g_RTConstantsBuffer.RTScale = lbvh->scale;
	// Set the Raytracing constants
	//_deviceResources->InitPSRTConstantsBuffer(
	//	_deviceResources->_RTConstantsBuffer.GetAddressOf(), &g_RTConstantsBuffer);

	// Update the matrices buffer
	D3D11_MAPPED_SUBRESOURCE map;
	ZeroMemory(&map, sizeof(D3D11_MAPPED_SUBRESOURCE));
	HRESULT hr = context->Map(_RTMatrices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (SUCCEEDED(hr)) {
		memcpy(map.pData, transformWorldViewInv.get(), sizeof(Matrix4));
		context->Unmap(_RTMatrices.Get(), 0);
	}
	else
		log_debug("[DBG] [BVH] Failed when mapping _RTMatrices: 0x%x", hr);

	// Non-embedded geometry:
	/*
	// Set the Raytracing SRVs
	ID3D11ShaderResourceView *srvs[] = {
		_RTBvhSRV.Get(),
		_RTVerticesSRV.Get(),
		_RTIndicesSRV.Get()
	};
	// Slots 14-16 are used for Raytracing buffers (BVH, Vertices and Indices)
	context->PSSetShaderResources(14, 3, srvs);
	*/

	// Embedded Geometry:
	//context->PSSetShaderResources(14, 1, _RTBvhSRV.GetAddressOf());
	ID3D11ShaderResourceView *srvs[] = {
		_RTBvhSRV.Get(),
		_RTMatricesSRV.Get(),
	};
	// Slots 14-15 are used for Raytracing buffers (BVH and Matrices)
	context->PSSetShaderResources(14, 2, srvs);
}

/*
 * Returns the CraftInstance associated to an objectId. Uses g_objectIdToIndex
 * to check if objectId has been cached. If it isn't, then *objects is searched
 * for objectId and then an entry is added to the g_objectIdToIndex cache.
 */
CraftInstance *EffectsRenderer::ObjectIDToCraftInstance(int objectId)
{
	int objIndex = -1;
	if (objects == NULL) return nullptr;

	// Have we cached the objectId?
	auto it = g_objectIdToIndex.find(objectId);
	if (it == g_objectIdToIndex.end()) {
		// There's no entry for this objId, find it and add it
		for (int i = 0; i < *g_XwaObjectsCount; i++) {
			ObjectEntry *object = &((*objects)[i]);
			if (object == NULL) return nullptr;
			if (object->objectID == objectId) {
				objIndex = i;
				g_objectIdToIndex.insert(std::make_pair(objectId, objIndex));
				break;
			}
		}
	}
	else {
		// Get the cached index
		objIndex = it->second;
	}

	if (objIndex != -1) {
		ObjectEntry *object = &((*objects)[objIndex]);
		MobileObjectEntry *mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) return nullptr;
		CraftInstance *craftInstance = mobileObject->craftInstancePtr;
		return craftInstance;
	}

	return nullptr;
}

/*
 * Fetches the InstanceEvent associated with the given objectId or adds a new one
 * if it doesn't exist.
 */
InstanceEvent *EffectsRenderer::ObjectIDToInstanceEvent(int objectId, uint32_t materialId)
{
	uint64_t Id = InstEventIdFromObjectMaterialId(objectId, materialId);

	auto it = g_objectIdToInstanceEvent.find(Id);
	if (it == g_objectIdToInstanceEvent.end()) {
		// Add a new entry to g_objectIdToInstanceEvent
		log_debug("[DBG] [INST] New InstanceEvent added to objectId-matId: %d-%d",
			objectId, materialId);
		g_objectIdToInstanceEvent.insert(std::make_pair(Id, InstanceEvent()));
		auto &new_it = g_objectIdToInstanceEvent.find(Id);
		if (new_it != g_objectIdToInstanceEvent.end())
			return &new_it->second;
		else
			return nullptr;
	}
	else
		return &it->second;
}

void EffectsRenderer::MainSceneHook(const SceneCompData* scene)
{
	auto &context = _deviceResources->_d3dDeviceContext;
	auto &resources = _deviceResources;

	ComPtr<ID3D11Buffer> oldVSConstantBuffer;
	ComPtr<ID3D11Buffer> oldPSConstantBuffer;
	ComPtr<ID3D11ShaderResourceView> oldVSSRV[3];

	context->VSGetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSGetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSGetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	_deviceResources->InitRasterizerState(_rasterizerState);
	_deviceResources->InitSamplerState(_samplerState.GetAddressOf(), nullptr);

	if (scene->TextureAlphaMask == 0)
	{
		_deviceResources->InitBlendState(_solidBlendState, nullptr);
		_deviceResources->InitDepthStencilState(_solidDepthState, nullptr);
		_bIsTransparentCall = false;
	}
	else
	{
		_deviceResources->InitBlendState(_transparentBlendState, nullptr);
		_deviceResources->InitDepthStencilState(_transparentDepthState, nullptr);
		_bIsTransparentCall = true;
	}

	_deviceResources->InitViewport(&_viewport);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);
	_deviceResources->InitVertexShader(_vertexShader);
	ComPtr<ID3D11PixelShader> lastPixelShader = g_bInTechRoom ? _techRoomPixelShader : _pixelShader;
	_deviceResources->InitPixelShader(lastPixelShader);

	UpdateTextures(scene);
	UpdateMeshBuffers(scene);
	UpdateVertexAndIndexBuffers(scene);
	UpdateConstantBuffer(scene);

	// Effects Management starts here.
	// Do the state management
	DoStateManagement(scene);

	// DEBUG
	// The scene pointer seems to be the same for every draw call, but the contents change.
	// scene->TextureName seems to be NULL all the time.
	// We can now have texture names associated with a specific ship instance. Meshes and faceData
	// can be rendered multiple times per frame, but we object ID is unique. We also have access to
	// the MobileObjectEntry and the CraftInstance. In other words: we can now apply effects on a
	// per-ship, per-texture basis. See the example below...
	//log_debug("[DBG] Rendering scene: 0x%x, faceData: 0x%x %s",
	//	scene, scene->FaceIndices, g_isInRenderLasers ? "LASER" : "");
	//MobileObjectEntry *pMobileObject = (MobileObjectEntry *)scene->pObject->pMobileObject;
	//log_debug("[DBG] FaceData: 0x%x, Id: %d, Species: %d, Category: %d",
	//	scene->FaceIndices, scene->pObject->ObjectId, scene->pObject->ObjectSpecies, scene->pObject->ShipCategory);
	// Show extra debug information on the current mesh:
	/*
	if (g_bDumpSSAOBuffers && bD3DDumpOBJEnabled)
		log_debug("[DBG] [%s]: Mesh: 0x%x, faceData: 0x%x, Id: %d, Type: %d, Genus: %d, Player: %d",
			_bLastTextureSelectedNotNULL ? _lastTextureSelected->_name.c_str() : "(null)", scene->MeshVertices, scene->FaceIndices,
			scene->pObject->ObjectId, scene->pObject->ObjectSpecies, scene->pObject->ShipCategory, *g_playerIndex);
	*/

	/*
	// The preybird cockpit re-uses the same mesh, but different faceData:
	[500] [DBG] [opt,FlightModels\PreybirdFighterCockpit.opt,TEX00051,color,0]: Mesh: 0x183197a3, faceData: 0x18319bac, Id: 3, Type: 29, Genus: 0
	[500] [DBG] [opt,FlightModels\PreybirdFighterCockpit.opt,TEX00052,color,0]: Mesh: 0x183197a3, faceData: 0x18319ddd, Id: 3, Type: 29, Genus: 0
	[500] [DBG] [opt,FlightModels\PreybirdFighterCockpit.opt,TEX00054,color,0]: Mesh: 0x183197a3, faceData: 0x1831a00e, Id: 3, Type: 29, Genus: 0
	[500] [DBG] [opt,FlightModels\PreybirdFighterCockpit.opt,TEX00053,color,0]: Mesh: 0x183197a3, faceData: 0x1831a23f, Id: 3, Type: 29, Genus: 0

	// These are several different TIE Fighters: the faceData and Texname are repeated, but we see different IDs:
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dde3181, faceData : 0x1ddecd62, Id : 8,  Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dd3f0f2, faceData : 0x1dd698c0, Id : 11, Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dde3181, faceData : 0x1ddecd62, Id : 11, Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dd3f0f2, faceData : 0x1dd698c0, Id : 10, Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dde3181, faceData : 0x1ddecd62, Id : 10, Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dd3f0f2, faceData : 0x1dd698c0, Id : 12, Type : 5, Genus : 0
	[500][DBG][opt, FlightModels\TieFighter.opt, TEX00029, color, 0] : Mesh : 0x1dde3181, faceData : 0x1ddecd62, Id : 12, Type : 5, Genus : 0
	*/

	/*
	if (stristr(_lastTextureSelected->_name.c_str(), "TIE") != 0 &&
		((stristr(_lastTextureSelected->_name.c_str(), "TEX00023") != 0 ||
		  stristr(_lastTextureSelected->_name.c_str(), "TEX00032") != 0))
	   )
	*/
	
	// Cache the current object's ID
	int objectId = -1;
	if (scene != nullptr && scene->pObject != nullptr)
		objectId = scene->pObject->ObjectId;
	const bool bInstanceEvent = _lastTextureSelected->material.bInstanceMaterial && objectId != -1;

	// UPDATE THE STATE OF INSTANCE EVENTS.
	// A material is associated with either a global ATC or an instance ATC for now.
	// Not sure if it would be legal to have both, but I'm going to simplify things.
	if (bInstanceEvent)
	{
		CraftInstance *craftInstance = ObjectIDToCraftInstance(objectId);
		InstanceEvent *instanceEvent = ObjectIDToInstanceEvent(objectId, _lastTextureSelected->material.Id);
		if (craftInstance != nullptr) {
			int hull = max(0, (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength)));
			int shields = craftInstance->ShieldPointsBack + craftInstance->ShieldPointsFront;
			// This value seems to be somewhat arbitrary. ISDs seem to be 741 when healthy,
			// and TIEs seem to be 628. But either way, this value is 0 when disabled. I think.
			int subsystems = craftInstance->SubsystemStatus;

			if (_lastTextureSelected->material.SkipWhenDisabled && subsystems == 0)
			{
				//log_debug("[DBG] [%s], systems: %d", _lastTextureSelected->_name.c_str(), systems);
				goto out;
			}
			
			if (instanceEvent != nullptr) {
				// Update the event for this instance
				instanceEvent->ShieldEvent = IEVT_NONE;
				instanceEvent->HullEvent = IEVT_NONE;

				if (shields == 0)
					instanceEvent->ShieldEvent = IEVT_SHIELDS_DOWN;

				if (50.0f < hull && hull <= 75.0f)
					instanceEvent->HullEvent = IEVT_HULL_DAMAGE_75;
				else if (25.0f < hull && hull <= 50.0f)
					instanceEvent->HullEvent = IEVT_HULL_DAMAGE_50;
				else if (hull <= 25.0f)
					instanceEvent->HullEvent = IEVT_HULL_DAMAGE_25;
			}
		}
	}

	// The main 3D content is rendered here, that includes the cockpit and 3D models. But
	// there's content that is still rendered in Direct3DDevice::Execute():
	// - The backdrop, including the Suns
	// - Engine Glow
	// - The HUD, including the reticle
	// - Explosions, including the DS2 core explosion
	/*
		We have an interesting mixture of Execute() calls and Hook calls. The sequence for
		each frame, looks like this:
		[11528][DBG] * ****************** PRESENT 3D
		[11528][DBG] BeginScene <-- Old method
		[11528][DBG] SceneBegin <-- New Hook
		[11528][DBG] Execute(1) <-- Old method (the backdrop is probably rendered here)
		[17076][DBG] EffectsRenderer::MainSceneHook
		[17076][DBG] EffectsRenderer::MainSceneHook
		... a bunch of calls to MainSceneHook. Most of the 3D content is rendered here ...
		[17076][DBG] EffectsRenderer::MainSceneHook
		[11528][DBG] Execute(1) <-- The engine glow might be rendered here (?)
		[11528][DBG] Execute(1) <-- Maybe the HUD and reticle is rendered here (?)
		[11528][DBG] EndScene   <-- Old method
		[11528][DBG] SceneEnd   <-- New Hook
		[11528][DBG] * ****************** PRESENT 3D
	*/
	g_PSCBuffer = { 0 };
	g_PSCBuffer.brightness = MAX_BRIGHTNESS;
	g_PSCBuffer.fBloomStrength = 1.0f;
	g_PSCBuffer.fPosNormalAlpha = 1.0f;
	g_PSCBuffer.fSSAOAlphaMult = g_fSSAOAlphaOfs;
	g_PSCBuffer.fSSAOMaskVal = g_DefaultGlobalMaterial.Metallic * 0.5f;
	g_PSCBuffer.fGlossiness = g_DefaultGlobalMaterial.Glossiness;
	g_PSCBuffer.fSpecInt = g_DefaultGlobalMaterial.Intensity;  // DEFAULT_SPEC_INT;
	g_PSCBuffer.fNMIntensity = g_DefaultGlobalMaterial.NMIntensity;
	g_PSCBuffer.AuxColor.x = 1.0f;
	g_PSCBuffer.AuxColor.y = 1.0f;
	g_PSCBuffer.AuxColor.z = 1.0f;
	g_PSCBuffer.AuxColor.w = 1.0f;
	g_PSCBuffer.AspectRatio = 1.0f;
	if (g_bInTechRoom && g_config.OnlyGrayscaleInTechRoom)
		g_PSCBuffer.special_control.ExclusiveMask = SPECIAL_CONTROL_GRAYSCALE;

	// Initialize the mesh transform for each mesh. During MainSceneHook,
	// this transform may be modified to apply an animation. See
	// ApplyDiegeticCockpit() and ApplyMeshTransform()
	g_OPTMeshTransformCB.MeshTransform.identity();

	// We will be modifying the regular render state from this point on. The state and the Pixel/Vertex
	// shaders are already set by this point; but if we modify them, we'll set bModifiedShaders to true
	// so that we can restore the state at the end of the draw call.
	_bModifiedShaders = false;
	_bModifiedPixelShader = false;
	_bModifiedBlendState = false;
	_bModifiedSamplerState = false;

	// Apply specific material properties for the current texture
	ApplyMaterialProperties();

	// Apply the SSAO mask/Special materials, like lasers and HUD
	ApplySpecialMaterials();

	ApplyNormalMapping();

	// Animate the Diegetic Cockpit (joystick, throttle, hyper-throttle, etc)
	ApplyDiegeticCockpit();

	// Animate the current mesh (if applicable)
	ApplyMeshTransform();

	if (g_bInTechRoom)
		ApplyRTShadows();

	// EARLY EXIT 1: Render the targetted craft to the Dynamic Cockpit RTVs and continue
	if (g_bDynCockpitEnabled && (g_bIsFloating3DObject || g_isInRenderMiniature)) {
		DCCaptureMiniature();
		goto out;
	}

	// Modify the state for both VR and regular game modes...

	// Maintain the k-closest lasers to the camera (but ignore the miniature lasers)
	if ((g_bEnableLaserLights && _bIsLaser && _bHasMaterial && !g_bStartedGUI) ||
		_lastTextureSelected->material.IsLightEmitter)
		AddLaserLights(scene);

	// Apply BLOOM flags and 32-bit mode enhancements
	// TODO: This code expects either a lightmap or a regular texture, but now we can have both at the same time
	// this will mess up all the animation logic when both the regular and lightmap layers have animations
	ApplyBloomSettings();

	// Transparent textures are currently used with DC to render floating text. However, if the erase region
	// commands are being ignored, then this will cause the text messages to be rendered twice. To avoid
	// having duplicated messages, we're removing these textures here when the erase_region commmands are
	// not being applied.
	// The above behavior is overridden if the DC element is set as "always_visible". In that case, the
	// transparent layer will remain visible even when the HUD is displayed.
	if (g_bDCManualActivate && g_bDynCockpitEnabled && _bDCIsTransparent && !g_bDCApplyEraseRegionCommands && !_bDCElemAlwaysVisible)
		goto out;

	// Dynamic Cockpit: Replace textures at run-time. Returns true if we need to skip the current draw call
	if (DCReplaceTextures())
		goto out;

	// TODO: Update the Hyperspace FSM -- but only update it exactly once per frame.
	// Looks like the code to do this update in Execute() still works. So moving on for now

	// Capture the cockpit OPT -> View transform for use in ShadowMapping later on
	if (g_bShadowMapEnable) {
		if (!_bCockpitConstantsCaptured && _bIsCockpit)
		{
			_bCockpitConstantsCaptured = true;
			_CockpitConstants = _constants;
			_CockpitWorldView = scene->WorldViewTransform;
		}
	}

	// Procedural Lava
	if (g_bProceduralLava && _bLastTextureSelectedNotNULL && _bHasMaterial && _lastTextureSelected->material.IsLava)
		ApplyProceduralLava();

	ApplyGreebles();

	if (g_bEnableAnimations)
		ApplyAnimatedTextures(objectId, bInstanceEvent);

	// Additional processing for VR or similar. Not implemented in this class, but will be in
	// other subclasses.
	ExtraPreprocessing();

	// Apply the changes to the vertex and pixel shaders
	//if (bModifiedShaders) 
	{
		resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);
		resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
		if (g_PSCBuffer.DynCockpitSlots > 0)
			resources->InitPSConstantBufferDC(resources->_PSConstantBufferDC.GetAddressOf(), &g_DCPSCBuffer);
		// Set the current mesh transform
		resources->InitVSConstantOPTMeshTransform(resources->_OPTMeshTransformCB.GetAddressOf(), &g_OPTMeshTransformCB);
	}

	// Dump the current scene to an OBJ file
	if (g_bDumpSSAOBuffers && bD3DDumpOBJEnabled) {
		// The coordinates are in Object (OPT) space and scale, centered on the origin.
		//OBJDump(scene->MeshVertices, *(int*)((int)scene->MeshVertices - 8));

		// This function is called once per face group. Each face group is associated with a single texture.
		// A single mesh can contain multiple face groups.
		// An OPT contains multiple meshes
		// _verticesCount has the number of vertices in the current face group
		//log_debug("[DBG] _vertices.size(): %lu, _verticesCount: %d", _vertices.size(), _verticesCount);
		OBJDumpD3dVertices(scene, Matrix4().identity());
	}

	// There's a bug with the lasers: they are sometimes rendered at the start of the frame, causing them to be
	// displayed *behind* the geometry. To fix this, we're going to save all the lasers in a list and then render
	// them at the end of the frame.
	// TODO: Instead of storing draw calls, use a deferred context to record the calls and then execute it later
	if (_bIsLaser) {
		DrawCommand command;
		// There's apparently a bug in the latest D3DRendererHook ddraw: the miniature does not set the proper
		// viewport, so lasers and other projectiles that are rendered in the CMD also show in the bottom of the
		// screen. This is not a fix, but a workaround: we're going to skip rendering any such objects if we've
		// started rendering the CMD.
		// Also, it looks like we can't use g_isInRenderMiniature for this check, since that doesn't seem to work
		// in some cases; we need to use g_bIsFloating3DObject instead.
		if (g_bStartedGUI || g_bIsFloating3DObject || g_isInRenderMiniature)
			goto out;

		// Save the current draw command and skip. We'll render the lasers later
		// Save the textures
		command.colortex = _lastTextureSelected;
		command.lighttex = _lastLightmapSelected;
		// Save the SRVs
		command.vertexSRV = _lastMeshVerticesView;
		command.normalsSRV = _lastMeshVertexNormalsView;
		command.tangentsSRV = _lastMeshVertexTangentsView;
		command.texturesSRV = _lastMeshTextureVerticesView;
		// Save the vertex and index buffers
		command.vertexBuffer = _lastVertexBuffer;
		command.indexBuffer = _lastIndexBuffer;
		command.trianglesCount = _trianglesCount;
		// Save the constants
		command.constants = _constants;
		command.meshTransformMatrix.identity();
		// Add the command to the list of deferred commands
		_LaserDrawCommands.push_back(command);

		// Do not render the laser at this moment
		goto out;
	}

	// Transparent polygons are sometimes rendered in the middle of a frame, causing them to appear
	// behind other geometry. We need to store those draw calls and render them later, near the end
	// of the frame.
	// TODO: Instead of storing draw calls, use a deferred context to record the calls and then execute it later
	if (_bIsTransparentCall) {
		DrawCommand command;
		// Save the current draw command and skip. We'll render the transparency later

		// Save the textures. The following will increase the refcount on the SRVs, we need
		// to decrease it later to avoid memory leaks
		for (int i = 0; i < 2; i++)
			command.SRVs[i] = nullptr;
		context->PSGetShaderResources(0, 2, command.SRVs);
		// Save the Vertex, Normal and UVs SRVs
		command.vertexSRV = _lastMeshVerticesView;
		command.normalsSRV = _lastMeshVertexNormalsView;
		command.tangentsSRV = _lastMeshVertexTangentsView;
		command.texturesSRV = _lastMeshTextureVerticesView;
		// Save the vertex and index buffers
		command.vertexBuffer = _lastVertexBuffer;
		command.indexBuffer = _lastIndexBuffer;
		command.trianglesCount = _trianglesCount;
		// Save the constants
		command.constants = _constants;
		// Save extra data
		command.PSCBuffer = g_PSCBuffer;
		command.DCPSCBuffer = g_DCPSCBuffer;
		command.bIsCockpit = _bIsCockpit;
		command.bIsGunner = _bIsGunner;
		command.bIsBlastMark = _bIsBlastMark;
		command.pixelShader = resources->GetCurrentPixelShader();
		command.meshTransformMatrix = g_OPTMeshTransformCB.MeshTransform;
		// Add the command to the list of deferred commands
		_TransparentDrawCommands.push_back(command);

		goto out;
	}

	RenderScene();

out:
	// The hyperspace effect needs the current VS constants to work properly
	if (g_HyperspacePhaseFSM == HS_INIT_ST)
		context->VSSetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSSetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());

	if (_bModifiedPixelShader)
		resources->InitPixelShader(lastPixelShader);

	// Decrease the refcount of all the objects we queried at the prologue. (Is this
	// really necessary? They live on the stack, so maybe they are auto-released?)
	oldVSConstantBuffer.Release();
	oldPSConstantBuffer.Release();
	for (int i = 0; i < 3; i++)
		oldVSSRV[i].Release();

	/*
	if (bModifiedBlendState) {
		RestoreBlendState();
		bModifiedBlendState = false;
	}
	*/

#if LOGGER_DUMP
	DumpConstants(_constants);
	DumpVector3(scene->MeshVertices, *(int*)((int)scene->MeshVertices - 8));
	DumpTextureVertices(scene->MeshTextureVertices, *(int*)((int)scene->MeshTextureVertices - 8));
	DumpD3dVertices(_vertices.data(), _verticesCount);
#endif
}

/*
 If the game is rendering the hyperspace effect, this function will select shaderToyBuf
 when rendering the cockpit. Otherwise it will select the regular offscreenBuffer
 */
inline ID3D11RenderTargetView *EffectsRenderer::SelectOffscreenBuffer(bool bIsMaskable, bool bSteamVRRightEye) {
	auto& resources = this->_deviceResources;

	ID3D11RenderTargetView *regularRTV = bSteamVRRightEye ? resources->_renderTargetViewR.Get() : resources->_renderTargetView.Get();
	ID3D11RenderTargetView *shadertoyRTV = bSteamVRRightEye ? resources->_shadertoyRTV_R.Get() : resources->_shadertoyRTV.Get();
	if (g_HyperspacePhaseFSM != HS_INIT_ST && bIsMaskable)
		// If we reach this point, then the game is in hyperspace AND this is a cockpit texture
		return shadertoyRTV;
	else
		// Normal output buffer (_offscreenBuffer)
		return regularRTV;
}

// This function should only be called when the miniature (targetted craft) is being rendered.
void EffectsRenderer::DCCaptureMiniature()
{
	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;

	// The viewport for the miniature is not properly set at the moment. So lasers and
	// projectiles (?) and maybe other objects show outside the CMD. We need to avoid
	// capturing them.
	if (_bIsLaser || _lastTextureSelected->is_Missile) return;

	// Remember the current scissor rect before modifying it
	UINT NumRects = 1;
	D3D11_RECT rect;
	context->RSGetScissorRects(&NumRects, &rect);

	unsigned short scissorLeft = *(unsigned short*)0x07D5244;
	unsigned short scissorTop = *(unsigned short*)0x07CA354;
	unsigned short scissorWidth = *(unsigned short*)0x08052B8;
	unsigned short scissorHeight = *(unsigned short*)0x07B33BC;
	float scaleX = _viewport.Width / _deviceResources->_displayWidth;
	float scaleY = _viewport.Height / _deviceResources->_displayHeight;
	D3D11_RECT scissor{};
	// The scissor is in screen coordinates.
	scissor.left = (LONG)(_viewport.TopLeftX + scissorLeft * scaleX + 0.5f);
	scissor.top = (LONG)(_viewport.TopLeftY + scissorTop * scaleY + 0.5f);
	scissor.right = scissor.left + (LONG)(scissorWidth * scaleX + 0.5f);
	scissor.bottom = scissor.top + (LONG)(scissorHeight * scaleY + 0.5f);
	_deviceResources->InitScissorRect(&scissor);

	// Apply the brightness settings to the pixel shader
	g_PSCBuffer.brightness = g_fBrightness;
	// Restore the non-VR dimensions:`
	_deviceResources->InitViewport(&_viewport);
	//resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
	resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);
	_deviceResources->InitVertexShader(_vertexShader);
	_deviceResources->InitPixelShader(_pixelShader);
	// Select the proper RTV
	context->OMSetRenderTargets(1, resources->_renderTargetViewDynCockpit.GetAddressOf(),
		resources->_depthStencilViewL.Get());

	// Enable Z-Buffer since we're drawing the targeted craft
	QuickSetZWriteEnabled(TRUE);

	// Render
	context->DrawIndexed(_trianglesCount * 3, 0, 0);
	g_iHUDOffscreenCommandsRendered++;

	// Restore the regular texture, RTV, shaders, etc:
	context->PSSetShaderResources(0, 1, _lastTextureSelected->_textureView.GetAddressOf());
	context->OMSetRenderTargets(1, resources->_renderTargetView.GetAddressOf(),
		resources->_depthStencilViewL.Get());
	/*
	if (g_bEnableVR) {
		resources->InitVertexShader(resources->_sbsVertexShader);
		// Restore the right constants in case we're doing VR rendering
		g_VSCBuffer.viewportScale[0] = 1.0f / displayWidth;
		g_VSCBuffer.viewportScale[1] = 1.0f / displayHeight;
		resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
	}
	else {
		resources->InitVertexShader(resources->_vertexShader);
	}
	*/
	// Restore the Pixel Shader constant buffers:
	g_PSCBuffer.brightness = MAX_BRIGHTNESS;
	resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);

	// Restore the scissor rect to its previous value
	_deviceResources->InitScissorRect(&rect);

	if (g_bDumpSSAOBuffers) {
		DirectX::SaveWICTextureToFile(context, resources->_offscreenBufferDynCockpit, GUID_ContainerFormatJpeg, L"c:\\temp\\_DC-FG-Input-Raw.jpg");
	}
}

bool EffectsRenderer::DCReplaceTextures()
{
	bool bSkip = false;
	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;

	// Dynamic Cockpit: Replace textures at run-time:
	if (!g_bDCManualActivate || !g_bDynCockpitEnabled || !_bLastTextureSelectedNotNULL || !_lastTextureSelected->is_DynCockpitDst ||
		// We should never render lightmap textures with the DC pixel shader:
		_lastTextureSelected->is_DynCockpitAlphaOverlay) {
		bSkip = false;
		goto out;
	}

	int idx = _lastTextureSelected->DCElementIndex;

	if (g_HyperspacePhaseFSM != HS_INIT_ST) {
		// If we're in hyperspace, let's set the corresponding flag before rendering DC controls
		_bModifiedShaders = true;
		g_PSCBuffer.bInHyperspace = 1;
	}

	// Check if this idx is valid before rendering
	if (idx >= 0 && idx < g_iNumDCElements) {
		dc_element *dc_element = &g_DCElements[idx];
		if (dc_element->bActive) {
			_bModifiedShaders = true;
			g_PSCBuffer.fBloomStrength = g_BloomConfig.fCockpitStrength;
			int numCoords = 0;
			for (int i = 0; i < dc_element->coords.numCoords; i++)
			{
				int src_slot = dc_element->coords.src_slot[i];
				// Skip invalid src slots
				if (src_slot < 0)
					continue;

				if (src_slot >= (int)g_DCElemSrcBoxes.src_boxes.size()) {
					//log_debug("[DBG] [DC] src_slot: %d bigger than src_boxes.size! %d",
					//	src_slot, g_DCElemSrcBoxes.src_boxes.size());
					continue;
				}

				DCElemSrcBox *src_box = &g_DCElemSrcBoxes.src_boxes[src_slot];
				// Skip src boxes that haven't been computed yet
				if (!src_box->bComputed)
					continue;

				uvfloat4 uv_src;
				uv_src.x0 = src_box->coords.x0; uv_src.y0 = src_box->coords.y0;
				uv_src.x1 = src_box->coords.x1; uv_src.y1 = src_box->coords.y1;
				g_DCPSCBuffer.src[numCoords] = uv_src;
				g_DCPSCBuffer.dst[numCoords] = dc_element->coords.dst[i];
				g_DCPSCBuffer.noisy_holo = _bIsNoisyHolo;
				g_DCPSCBuffer.transparent = _bDCIsTransparent;
				g_DCPSCBuffer.use_damage_texture = false;
				if (_bWarheadLocked)
					g_DCPSCBuffer.bgColor[numCoords] = dc_element->coords.uWHColor[i];
				else
					g_DCPSCBuffer.bgColor[numCoords] = _bIsTargetHighlighted ?
					dc_element->coords.uHGColor[i] :
					dc_element->coords.uBGColor[i];
				// The hologram property will make *all* uvcoords in this DC element
				// holographic as well:
				//bIsHologram |= (dc_element->bHologram);
				numCoords++;
			} // for
			// g_bDCHologramsVisible is a hard switch, let's use g_fDCHologramFadeIn instead to
			// provide a softer ON/OFF animation
			if (_bIsHologram && g_fDCHologramFadeIn <= 0.01f) {
				bSkip = true;
				goto out;
			}
			g_PSCBuffer.DynCockpitSlots = numCoords;
			//g_PSCBuffer.bUseCoverTexture = (dc_element->coverTexture != nullptr) ? 1 : 0;
			g_PSCBuffer.bUseCoverTexture = (resources->dc_coverTexture[idx] != nullptr) ? 1 : 0;

			// slot 0 is the cover texture
			// slot 1 is the HUD offscreen buffer
			// slot 2 is the text buffer
			context->PSSetShaderResources(1, 1, resources->_offscreenAsInputDynCockpitSRV.GetAddressOf());
			context->PSSetShaderResources(2, 1, resources->_DCTextSRV.GetAddressOf());
			// Set the cover texture:
			if (g_PSCBuffer.bUseCoverTexture) {
				//log_debug("[DBG] [DC] Setting coverTexture: 0x%x", resources->dc_coverTexture[idx].GetAddressOf());
				//context->PSSetShaderResources(0, 1, dc_element->coverTexture.GetAddressOf());
				//context->PSSetShaderResources(0, 1, &dc_element->coverTexture);
				context->PSSetShaderResources(0, 1, resources->dc_coverTexture[idx].GetAddressOf());
				//resources->InitPSShaderResourceView(resources->dc_coverTexture[idx].Get());
			}
			else
				context->PSSetShaderResources(0, 1, _lastTextureSelected->_textureView.GetAddressOf());
			//resources->InitPSShaderResourceView(lastTextureSelected->_textureView.Get());
		// No need for an else statement, slot 0 is already set to:
		// context->PSSetShaderResources(0, 1, texture->_textureView.GetAddressOf());
		// See D3DRENDERSTATE_TEXTUREHANDLE, where lastTextureSelected is set.
			if (g_PSCBuffer.DynCockpitSlots > 0) {
				_bModifiedPixelShader = true;
				if (_bIsHologram) {
					// Holograms require alpha blending to be enabled, but we also need to save the current
					// blending state so that it gets restored at the end of this draw call.
					//SaveBlendState();
					EnableHoloTransparency();
					_bModifiedBlendState = true;
					uint32_t hud_color = (*g_XwaFlightHudColor) & 0x00FFFFFF;
					//log_debug("[DBG] hud_color, border, inside: 0x%x, 0x%x", *g_XwaFlightHudBorderColor, *g_XwaFlightHudColor);
					g_ShadertoyBuffer.iTime = g_fDCHologramTime;
					g_ShadertoyBuffer.twirl = g_fDCHologramFadeIn;
					// Override the background color if the current DC element is a hologram:
					g_DCPSCBuffer.bgColor[0] = hud_color;
					resources->InitPSConstantBufferHyperspace(resources->_hyperspaceConstantBuffer.GetAddressOf(), &g_ShadertoyBuffer);
				}
				resources->InitPixelShader(_bIsHologram ? resources->_pixelShaderDCHolo : resources->_pixelShaderDC);
			}
			else if (g_PSCBuffer.bUseCoverTexture) {
				_bModifiedPixelShader = true;
				resources->InitPixelShader(resources->_pixelShaderEmptyDC);
			}
		} // if dc_element->bActive
	}

out:
	return bSkip;
}

void EffectsRenderer::RenderScene()
{
	if (_deviceResources->_displayWidth == 0 || _deviceResources->_displayHeight == 0)
	{
		return;
	}

	// Skip hangar shadows if configured:
	if (g_rendererType == RendererType_Shadow && !g_config.HangarShadowsEnabled)
		return;

	auto &context = _deviceResources->_d3dDeviceContext;

	unsigned short scissorLeft = *(unsigned short*)0x07D5244;
	unsigned short scissorTop = *(unsigned short*)0x07CA354;
	unsigned short scissorWidth = *(unsigned short*)0x08052B8;
	unsigned short scissorHeight = *(unsigned short*)0x07B33BC;
	float scaleX = _viewport.Width / _deviceResources->_displayWidth;
	float scaleY = _viewport.Height / _deviceResources->_displayHeight;
	D3D11_RECT scissor{};
	// The scissor is in screen coordinates.
	scissor.left = (LONG)(_viewport.TopLeftX + scissorLeft * scaleX + 0.5f);
	scissor.top = (LONG)(_viewport.TopLeftY + scissorTop * scaleY + 0.5f);
	scissor.right = scissor.left + (LONG)(scissorWidth * scaleX + 0.5f);
	scissor.bottom = scissor.top + (LONG)(scissorHeight * scaleY + 0.5f);
	_deviceResources->InitScissorRect(&scissor);

	// This method isn't called to draw the hyperstreaks or the hypertunnel. A different
	// (unknown, maybe RenderMain?) path is taken instead.

	ID3D11RenderTargetView *rtvs[6] = {
		SelectOffscreenBuffer(_bIsCockpit || _bIsGunner /* || _bIsReticle */), // Select the main RTV
		_deviceResources->_renderTargetViewBloomMask.Get(),
		g_bAOEnabled ? _deviceResources->_renderTargetViewDepthBuf.Get() : NULL,
		// The normals hook should not be allowed to write normals for light textures. This is now implemented
		// in XwaD3dPixelShader
		_deviceResources->_renderTargetViewNormBuf.Get(),
		// Blast Marks are confused with glass because they are not shadeless; but they have transparency
		_bIsBlastMark ? NULL : _deviceResources->_renderTargetViewSSAOMask.Get(),
		_bIsBlastMark ? NULL : _deviceResources->_renderTargetViewSSMask.Get(),
	};
	context->OMSetRenderTargets(6, rtvs, _deviceResources->_depthStencilViewL.Get());

	// DEBUG: Skip draw calls if we're debugging the process
	/*
	if (g_iD3DExecuteCounterSkipHi > -1 && g_iD3DExecuteCounter > g_iD3DExecuteCounterSkipHi)
		goto out;
	if (g_iD3DExecuteCounterSkipLo > -1 && g_iD3DExecuteCounter < g_iD3DExecuteCounterSkipLo)
		goto out;
	*/

	context->DrawIndexed(_trianglesCount * 3, 0, 0);

//out:
	g_iD3DExecuteCounter++;
	g_iDrawCounter++; // We need this counter to enable proper Tech Room detection
}

void EffectsRenderer::RenderLasers()
{
	if (_LaserDrawCommands.size() == 0)
		return;

	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;
	//log_debug("[DBG] Rendering %d deferred draw calls", _LaserDrawCommands.size());

	SaveContext();

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	// Set the proper rastersize and depth stencil states for transparency
	_deviceResources->InitBlendState(_transparentBlendState, nullptr);
	_deviceResources->InitDepthStencilState(_transparentDepthState, nullptr);

	_deviceResources->InitViewport(&_viewport);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);
	_deviceResources->InitVertexShader(_vertexShader);
	_deviceResources->InitPixelShader(_pixelShader);

	g_PSCBuffer = { 0 };
	g_PSCBuffer.brightness = MAX_BRIGHTNESS;
	g_PSCBuffer.fBloomStrength = 1.0f;
	g_PSCBuffer.fPosNormalAlpha = 1.0f;
	g_PSCBuffer.fSSAOAlphaMult = g_fSSAOAlphaOfs;
	g_PSCBuffer.AspectRatio = 1.0f;

	// Laser-specific stuff from ApplySpecialMaterials():
	g_PSCBuffer.fSSAOMaskVal = EMISSION_MAT;
	g_PSCBuffer.fGlossiness = DEFAULT_GLOSSINESS;
	g_PSCBuffer.fSpecInt = DEFAULT_SPEC_INT;
	g_PSCBuffer.fNMIntensity = 0.0f;
	g_PSCBuffer.fSpecVal = 0.0f;
	g_PSCBuffer.bIsShadeless = 1;
	g_PSCBuffer.fPosNormalAlpha = 0.0f;
	// Laser-specific stuff from ApplyBloomSettings():
	g_PSCBuffer.fBloomStrength = g_BloomConfig.fLasersStrength;
	//g_PSCBuffer.bIsLaser			= 2; // Enhance lasers by default

	g_OPTMeshTransformCB.MeshTransform.identity();

	// Flags used in RenderScene():
	_bIsCockpit = false;
	_bIsGunner = false;
	_bIsBlastMark = false;

	// Just in case we need to do anything for VR or other alternative display devices...
	ExtraPreprocessing();

	// Apply the VS and PS constants
	resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);
	//resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
	resources->InitVSConstantOPTMeshTransform(resources->_OPTMeshTransformCB.GetAddressOf(), &g_OPTMeshTransformCB);

	// Other stuff that is common in the loop below
	UINT vertexBufferStride = sizeof(D3dVertex);
	UINT vertexBufferOffset = 0;
	// Run the deferred commands
	for (DrawCommand command : _LaserDrawCommands) {
		// Set the textures
		_deviceResources->InitPSShaderResourceView(command.colortex->_textureView.Get(),
			command.lighttex == nullptr ? nullptr : command.lighttex->_textureView.Get());

		// Set the mesh buffers
		ID3D11ShaderResourceView* vsSSRV[4] = { command.vertexSRV, command.normalsSRV, command.texturesSRV, command.tangentsSRV };
		context->VSSetShaderResources(0, 4, vsSSRV);

		// Set the index and vertex buffers
		_deviceResources->InitVertexBuffer(nullptr, nullptr, nullptr);
		_deviceResources->InitVertexBuffer(&(command.vertexBuffer), &vertexBufferStride, &vertexBufferOffset);
		_deviceResources->InitIndexBuffer(nullptr, true);
		_deviceResources->InitIndexBuffer(command.indexBuffer, true);

		// Set the constants buffer
		context->UpdateSubresource(_constantBuffer, 0, nullptr, &(command.constants), 0, 0);

		// Set the number of triangles
		_trianglesCount = command.trianglesCount;

		// Render the deferred commands
		RenderScene();
	}

	// Clear the command list and restore the previous state
	_LaserDrawCommands.clear();
	RestoreContext();
}

void EffectsRenderer::RenderTransparency()
{
	if (_TransparentDrawCommands.size() == 0)
		return;

	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;

	SaveContext();

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	// Set the proper rastersize and depth stencil states for transparency
	_deviceResources->InitBlendState(_transparentBlendState, nullptr);
	_deviceResources->InitDepthStencilState(_transparentDepthState, nullptr);

	_deviceResources->InitViewport(&_viewport);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);
	_deviceResources->InitVertexShader(_vertexShader);

	// Just in case we need to do anything for VR or other alternative display devices...
	ExtraPreprocessing();

	// Other stuff that is common in the loop below
	UINT vertexBufferStride = sizeof(D3dVertex);
	UINT vertexBufferOffset = 0;

	// Run the deferred commands
	for (DrawCommand command : _TransparentDrawCommands) {
		g_PSCBuffer = command.PSCBuffer;
		g_DCPSCBuffer = command.DCPSCBuffer;

		// Flags used in RenderScene():
		_bIsCockpit = command.bIsCockpit;
		_bIsGunner = command.bIsGunner;
		_bIsBlastMark = command.bIsBlastMark;

		// Apply the VS and PS constants
		resources->InitPSConstantBuffer3D(resources->_PSConstantBuffer.GetAddressOf(), &g_PSCBuffer);
		//resources->InitVSConstantBuffer3D(resources->_VSConstantBuffer.GetAddressOf(), &g_VSCBuffer);
		g_OPTMeshTransformCB.MeshTransform = command.meshTransformMatrix;
		resources->InitVSConstantOPTMeshTransform(
			resources->_OPTMeshTransformCB.GetAddressOf(), &g_OPTMeshTransformCB);
		if (g_PSCBuffer.DynCockpitSlots > 0)
			resources->InitPSConstantBufferDC(resources->_PSConstantBufferDC.GetAddressOf(), &g_DCPSCBuffer);

		// Set the textures
		_deviceResources->InitPSShaderResourceView(command.SRVs[0], command.SRVs[1]);

		// Set the mesh buffers
		ID3D11ShaderResourceView* vsSSRV[4] = { command.vertexSRV, command.normalsSRV, command.texturesSRV, command.tangentsSRV };
		context->VSSetShaderResources(0, 4, vsSSRV);

		// Set the index and vertex buffers
		_deviceResources->InitVertexBuffer(nullptr, nullptr, nullptr);
		_deviceResources->InitVertexBuffer(&(command.vertexBuffer), &vertexBufferStride, &vertexBufferOffset);
		_deviceResources->InitIndexBuffer(nullptr, true);
		_deviceResources->InitIndexBuffer(command.indexBuffer, true);

		// Set the constants buffer
		context->UpdateSubresource(_constantBuffer, 0, nullptr, &(command.constants), 0, 0);

		// Set the number of triangles
		_trianglesCount = command.trianglesCount;

		// Set the right pixel shader
		_deviceResources->InitPixelShader(command.pixelShader);

		// Render the deferred commands
		RenderScene();

		// Decrease the refcount of the textures
		for (int i = 0; i < 2; i++)
			if (command.SRVs[i] != nullptr) command.SRVs[i]->Release();
	}

	// Clear the command list and restore the previous state
	_TransparentDrawCommands.clear();
	RestoreContext();
}

/*
 * Using the current 3D box limits loaded in g_OBJLimits, compute the 2D/Z-Depth limits
 * needed to center the Shadow Map depth buffer. This version uses the transforms in
 * g_CockpitConstants
 */
Matrix4 EffectsRenderer::GetShadowMapLimits(Matrix4 L, float *OBJrange, float *OBJminZ) {
	float minx = 100000.0f, maxx = -100000.0f;
	float miny = 100000.0f, maxy = -100000.0f;
	float minz = 100000.0f, maxz = -100000.0f;
	float cx, cy, sx, sy;
	Matrix4 S, T;
	Vector4 P, Q;
	Matrix4 WorldView = XwaTransformToMatrix4(_CockpitWorldView);
	FILE *file = NULL;

	if (g_bDumpSSAOBuffers) {
		fopen_s(&file, "./ShadowMapLimits.OBJ", "wt");

		XwaTransform M = _CockpitWorldView;
		log_debug("[DBG] -----------------------------------------------");
		log_debug("[DBG] GetShadowMapLimits WorldViewTransform:");
		log_debug("[DBG] %0.3f, %0.3f, %0.3f, %0.3f",
			M.Rotation._11, M.Rotation._12, M.Rotation._13, 0.0f);
		log_debug("[DBG] %0.3f, %0.3f, %0.3f, %0.3f",
			M.Rotation._21, M.Rotation._22, M.Rotation._23, 0.0f);
		log_debug("[DBG] %0.3f, %0.3f, %0.3f, %0.3f",
			M.Rotation._31, M.Rotation._32, M.Rotation._33, 0.0f);
		log_debug("[DBG] %0.3f, %0.3f, %0.3f, %0.3f",
			M.Position.x, M.Position.y, M.Position.z, 1.0f);
		log_debug("[DBG] -----------------------------------------------");
	}

	for (Vector4 X : g_OBJLimits) {
		// This transform chain should be the same we apply in ShadowMapVS.hlsl

		// OPT to camera view transform. First transform object space into view space:
		Q = WorldView * X;
		// Now, transform OPT coords to meters:
		Q *= OPT_TO_METERS;
		// Invert the Y-axis since our coordinate system has Y+ pointing up
		Q.y = -Q.y;
		// Just make sure w = 1
		Q.w = 1.0f;

		// The point is now in metric 3D, with the POV at the origin.
		// Apply the light transform, keep the points in metric 3D.
		P = L * Q;

		// Update the limits
		if (P.x < minx) minx = P.x;
		if (P.y < miny) miny = P.y;
		if (P.z < minz) minz = P.z;

		if (P.x > maxx) maxx = P.x;
		if (P.y > maxy) maxy = P.y;
		if (P.z > maxz) maxz = P.z;

		if (g_bDumpSSAOBuffers)
			fprintf(file, "v %0.6f %0.6f %0.6f\n", P.x, P.y, P.z);
	}

	if (g_bDumpSSAOBuffers) {
		fprintf(file, "\n");
		fprintf(file, "f 1 2 3\n");
		fprintf(file, "f 1 3 4\n");

		fprintf(file, "f 5 6 7\n");
		fprintf(file, "f 5 7 8\n");

		fprintf(file, "f 1 5 6\n");
		fprintf(file, "f 1 6 2\n");

		fprintf(file, "f 4 8 7\n");
		fprintf(file, "f 4 7 3\n");
		fflush(file);
		fclose(file);
	}

	// Compute the centroid
	cx = (minx + maxx) / 2.0f;
	cy = (miny + maxy) / 2.0f;

	// Compute the scale
	sx = 1.95f / (maxx - minx); // Map to -0.975..0.975
	sy = 1.95f / (maxy - miny); // Map to -0.975..0.975
	// Having an anisotropic scale provides a better usage of the shadow map. However
	// it also distorts the shadow map, making it harder to debug.
	// release
	float s = min(sx, sy);
	//sz = 1.8f / (maxz - minz); // Map to -0.9..0.9
	//sz = 1.0f / (maxz - minz);

	// We want to map xy to the origin; but we want to map Z to 0..0.98, so that Z = 1.0 is at infinity
	// Translate the points so that the centroid is at the origin
	T.translate(-cx, -cy, 0.0f);
	// Scale around the origin so that the xyz limits are [-0.9..0.9]
	if (g_ShadowMapping.bAnisotropicMapScale)
		S.scale(sx, sy, 1.0f); // Anisotropic scale: better use of the shadow map
	else
		S.scale(s, s, 1.0f); // Isotropic scale: better for debugging.

	*OBJminZ = minz;
	*OBJrange = maxz - minz;

	if (g_bDumpSSAOBuffers) {
		log_debug("[DBG] [SHW] min-x,y,z: %0.3f, %0.3f, %0.3f, max-x,y,z: %0.3f, %0.3f, %0.3f",
			minx, miny, minz, maxx, maxy, maxz);
		log_debug("[DBG] [SHW] cx,cy: %0.3f, %0.3f, sx,sy,s: %0.3f, %0.3f, %0.3f",
			cx, cy, sx, sy, s);
		log_debug("[DBG] [SHW] maxz: %0.3f, OBJminZ: %0.3f, OBJrange: %0.3f",
			maxz, *OBJminZ, *OBJrange);
		log_debug("[DBG] [SHW] sm_z_factor: %0.6f, FOVDistScale: %0.3f",
			g_ShadowMapVSCBuffer.sm_z_factor, g_ShadowMapping.FOVDistScale);
	}
	return S * T;
}

/*
 * Using the given 3D box Limits, compute the 2D/Z-Depth limits needed to center the Shadow Map depth
 * buffer. This version expects the AABB to be in the lightview coordinate system
 */
Matrix4 EffectsRenderer::GetShadowMapLimits(const AABB &aabb, float *range, float *minZ) {
	float cx, cy, sx, sy;
	Matrix4 S, T;
	Vector4 P, Q;

	// Compute the centroid
	cx = (aabb.min.x + aabb.max.x) / 2.0f;
	cy = (aabb.min.y + aabb.max.y) / 2.0f;

	// Compute the scale
	sx = 1.95f / (aabb.max.x - aabb.min.x); // Map to -0.975..0.975
	sy = 1.95f / (aabb.max.y - aabb.min.y); // Map to -0.975..0.975
	// Having an anisotropic scale provides a better usage of the shadow map. However
	// it also distorts the shadow map, making it harder to debug.
	// release
	float s = min(sx, sy);
	//sz = 1.8f / (maxz - minz); // Map to -0.9..0.9
	//sz = 1.0f / (maxz - minz);

	// We want to map xy to the origin; but we want to map Z to 0..0.98, so that Z = 1.0 is at infinity
	// Translate the points so that the centroid is at the origin
	T.translate(-cx, -cy, 0.0f);
	// Scale around the origin so that the xyz limits are [-0.9..0.9]
	if (g_ShadowMapping.bAnisotropicMapScale)
		S.scale(sx, sy, 1.0f); // Anisotropic scale: better use of the shadow map
	else
		S.scale(s, s, 1.0f); // Isotropic scale: better for debugging.

	*minZ = aabb.min.z;
	*range = aabb.max.z - aabb.min.z;

	if (g_bDumpSSAOBuffers) {
		log_debug("[DBG] [HNG] min-x,y,z: %0.3f, %0.3f, %0.3f, max-x,y,z: %0.3f, %0.3f, %0.3f",
			aabb.min.x, aabb.min.y, aabb.min.z,
			aabb.max.x, aabb.max.y, aabb.max.z);
		log_debug("[DBG] [HNG] cx,cy: %0.3f, %0.3f, sx,sy,s: %0.3f, %0.3f, %0.3f",
			cx, cy, sx, sy, s);
		log_debug("[DBG] [HNG] maxZ: %0.3f, minZ: %0.3f, range: %0.3f",
			aabb.max.z, *minZ, *range);
	}
	return S * T;
}

void EffectsRenderer::RenderCockpitShadowMap()
{
	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;

	// We're still tagging the lights in PrimarySurface::TagXWALights(). Here we just render
	// the ShadowMap.

	// TODO: The g_bShadowMapEnable was added later to be able to toggle the shadows with a hotkey
	//	     Either remove the multiplicity of "enable" variables or get rid of the hotkey.
	g_ShadowMapping.bEnabled = g_bShadowMapEnable;
	g_ShadowMapVSCBuffer.sm_enabled = g_bShadowMapEnable && g_ShadowMapping.bUseShadowOBJ;
	// The post-proc shaders (SSDOAddPixel, SSAOAddPixel) use sm_enabled to compute shadows,
	// we must set the PS constants here even if we're not rendering shadows at all
	resources->InitPSConstantBufferShadowMap(resources->_shadowMappingPSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);

	// Shadow Mapping is disabled when the we're in external view or traveling through hyperspace.
	// Maybe also disable it if the cockpit is hidden
	if (!g_ShadowMapping.bEnabled || !g_bShadowMapEnable || !g_ShadowMapping.bUseShadowOBJ || _bExternalCamera ||
		!_bCockpitDisplayed || g_HyperspacePhaseFSM != HS_INIT_ST || !_bCockpitConstantsCaptured ||
		_bShadowsRenderedInCurrentFrame)
		return;

	SaveContext();

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());

	// Enable ZWrite: we'll need it for the ShadowMap
	D3D11_DEPTH_STENCIL_DESC desc;
	ComPtr<ID3D11DepthStencilState> depthState;
	desc.DepthEnable = TRUE;
	desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = D3D11_COMPARISON_LESS;
	desc.StencilEnable = FALSE;
	resources->InitDepthStencilState(depthState, &desc);
	// Solid blend state, no transparency
	resources->InitBlendState(_solidBlendState, nullptr);

	// Init the Viewport. This viewport has the dimensions of the shadowmap texture
	_deviceResources->InitViewport(&g_ShadowMapping.ViewPort);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);

	_deviceResources->InitVertexShader(resources->_shadowMapVS);
	_deviceResources->InitPixelShader(resources->_shadowMapPS);

	// Set the vertex and index buffers
	UINT stride = sizeof(D3DTLVERTEX), ofs = 0;
	resources->InitVertexBuffer(resources->_shadowVertexBuffer.GetAddressOf(), &stride, &ofs);
	resources->InitIndexBuffer(resources->_shadowIndexBuffer.Get(), false);

	g_ShadowMapVSCBuffer.sm_PCSS_enabled = g_bShadowMapEnablePCSS;
	g_ShadowMapVSCBuffer.sm_resolution = (float)g_ShadowMapping.ShadowMapSize;
	g_ShadowMapVSCBuffer.sm_hardware_pcf = g_bShadowMapHardwarePCF;
	// Select either the SW or HW bias depending on which setting is enabled
	g_ShadowMapVSCBuffer.sm_bias = g_bShadowMapHardwarePCF ? g_ShadowMapping.hw_pcf_bias : g_ShadowMapping.sw_pcf_bias;
	g_ShadowMapVSCBuffer.sm_enabled = g_bShadowMapEnable;
	g_ShadowMapVSCBuffer.sm_debug = g_bShadowMapDebug;
	g_ShadowMapVSCBuffer.sm_VR_mode = g_bEnableVR;

	// Set the cockpit transform matrix and other shading-related constants
	context->UpdateSubresource(_constantBuffer, 0, nullptr, &(_CockpitConstants), 0, 0);

	// Compute all the lightWorldMatrices and their OBJrange/minZ's first:
	for (int idx = 0; idx < *s_XwaGlobalLightsCount; idx++)
	{
		float range, minZ;
		// Don't bother computing shadow maps for lights with a high black level
		if (g_ShadowMapVSCBuffer.sm_black_levels[idx] > 0.95f)
			continue;

		// Reset the range for each active light
		g_ShadowMapVSCBuffer.sm_minZ[idx] = 0.0f;
		g_ShadowMapVSCBuffer.sm_maxZ[idx] = DEFAULT_COCKPIT_SHADOWMAP_MAX_Z; // Regular range for the cockpit

		// g_OPTMeshTransformCB.MeshTransform is only relevant if we're going to apply
		// m	esh transforms to individual shadow map meshes. Like pieces of the diegetic cockpit.

		// Compute the LightView (Parallel Projection) Matrix
		// g_CurrentHeadingViewMatrix needs to have the correct data from SteamVR, including roll
		Matrix4 L = ComputeLightViewMatrix(idx, g_CurrentHeadingViewMatrix, false);
		Matrix4 ST = GetShadowMapLimits(L, &range, &minZ);

		g_ShadowMapVSCBuffer.lightWorldMatrix[idx] = ST * L;
		g_ShadowMapVSCBuffer.OBJrange[idx] = range;
		g_ShadowMapVSCBuffer.OBJminZ[idx] = minZ;

		// Render each light to its own shadow map
		g_ShadowMapVSCBuffer.light_index = idx;

		// Set the constant buffer for the VS and PS.
		resources->InitVSConstantBufferShadowMap(resources->_shadowMappingVSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);
		// The pixel shader is empty for the shadow map, but the SSAO/SSDO/Deferred PS do use these constants later
		resources->InitPSConstantBufferShadowMap(resources->_shadowMappingPSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);

		// Clear the Shadow Map DSV (I may have to update this later for the hyperspace state)
		context->ClearDepthStencilView(resources->_shadowMapDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
		// Set the Shadow Map DSV
		context->OMSetRenderTargets(0, 0, resources->_shadowMapDSV.Get());
		// Render the Shadow Map
		context->DrawIndexed(g_ShadowMapping.NumIndices, 0, 0);

		// Copy the shadow map to the right slot in the array
		context->CopySubresourceRegion(resources->_shadowMapArray, D3D11CalcSubresource(0, idx, 1), 0, 0, 0,
			resources->_shadowMap, D3D11CalcSubresource(0, 0, 1), NULL);
	}

	if (g_bDumpSSAOBuffers) {
		wchar_t wFileName[80];
		
		for (int i = 0; i < *s_XwaGlobalLightsCount; i++) {
			context->CopySubresourceRegion(resources->_shadowMapDebug, D3D11CalcSubresource(0, 0, 1), 0, 0, 0,
				resources->_shadowMapArray, D3D11CalcSubresource(0, i, 1), NULL);
			swprintf_s(wFileName, 80, L"c:\\Temp\\_shadowMap%d.dds", i);
			DirectX::SaveDDSTextureToFile(context, resources->_shadowMapDebug, wFileName);
		}
	}

	RestoreContext();
	_bShadowsRenderedInCurrentFrame = true;
}

void EffectsRenderer::RenderHangarShadowMap()
{
	auto &resources = _deviceResources;
	auto &context = resources->_d3dDeviceContext;

	if (!*g_playerInHangar)
		return;

	if (!g_bShadowMapEnable || _ShadowMapDrawCommands.size() == 0 || _bHangarShadowsRenderedInCurrentFrame ||
		g_HyperspacePhaseFSM != HS_INIT_ST) {
		_ShadowMapDrawCommands.clear();
		return;
	}

	// If there's no cockpit shadow map, we must disable the first shadow map slot, but continue rendering hangar shadows
	if (!g_ShadowMapping.bUseShadowOBJ || _bExternalCamera)
		g_ShadowMapVSCBuffer.sm_black_levels[0] = 1.0f;
	else 
		g_ShadowMapVSCBuffer.sm_black_levels[0] = 0.05f;
	// Make hangar shadows darker, as in the original version
	g_ShadowMapVSCBuffer.sm_black_levels[1] = 0.05f;

	// Adjust the range for the shadow maps. The first light is only for the cockpit:
	g_ShadowMapVSCBuffer.sm_minZ[0] = 0.0f;
	g_ShadowMapVSCBuffer.sm_maxZ[0] = DEFAULT_COCKPIT_SHADOWMAP_MAX_Z;
	// The second light is for the hangar:
	g_ShadowMapVSCBuffer.sm_minZ[1] = _bExternalCamera ? 0.0f : DEFAULT_COCKPIT_SHADOWMAP_MAX_Z - DEFAULT_COCKPIT_SHADOWMAP_MAX_Z / 2.0f;
	g_ShadowMapVSCBuffer.sm_maxZ[1] = DEFAULT_HANGAR_SHADOWMAP_MAX_Z;

	// TODO: The g_bShadowMapEnable was added later to be able to toggle the shadows with a hotkey
	//	     Either remove the multiplicity of "enable" variables or get rid of the hotkey.
	g_ShadowMapping.bEnabled = g_bShadowMapEnable;
	g_ShadowMapVSCBuffer.sm_enabled = g_bShadowMapEnable;
	// The post-proc shaders (SSDOAddPixel, SSAOAddPixel) use sm_enabled to compute shadows,
	// we must set the PS constants here even if we're not rendering shadows at all
	resources->InitPSConstantBufferShadowMap(resources->_shadowMappingPSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);

	SaveContext();

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());

	// Enable ZWrite: we'll need it for the ShadowMap
	D3D11_DEPTH_STENCIL_DESC desc;
	ComPtr<ID3D11DepthStencilState> depthState;
	desc.DepthEnable = TRUE;
	desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = D3D11_COMPARISON_LESS;
	desc.StencilEnable = FALSE;
	resources->InitDepthStencilState(depthState, &desc);
	// Solid blend state, no transparency
	resources->InitBlendState(_solidBlendState, nullptr);

	// Init the Viewport. This viewport has the dimensions of the shadowmap texture
	_deviceResources->InitViewport(&g_ShadowMapping.ViewPort);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);

	_deviceResources->InitVertexShader(resources->_hangarShadowMapVS);
	_deviceResources->InitPixelShader(resources->_shadowMapPS);

	g_ShadowMapVSCBuffer.sm_PCSS_enabled = g_bShadowMapEnablePCSS;
	g_ShadowMapVSCBuffer.sm_resolution = (float)g_ShadowMapping.ShadowMapSize;
	g_ShadowMapVSCBuffer.sm_hardware_pcf = g_bShadowMapHardwarePCF;
	// Select either the SW or HW bias depending on which setting is enabled
	g_ShadowMapVSCBuffer.sm_bias = g_bShadowMapHardwarePCF ? g_ShadowMapping.hw_pcf_bias : g_ShadowMapping.sw_pcf_bias;
	g_ShadowMapVSCBuffer.sm_enabled = g_bShadowMapEnable;
	g_ShadowMapVSCBuffer.sm_debug = g_bShadowMapDebug;
	g_ShadowMapVSCBuffer.sm_VR_mode = g_bEnableVR;

	// Other stuff that is common in the loop below
	UINT vertexBufferStride = sizeof(D3dVertex);
	UINT vertexBufferOffset = 0;

	// Shadow map limits
	float range, minZ;
	Matrix4 ST = GetShadowMapLimits(_hangarShadowAABB, &range, &minZ);

	// Compute all the lightWorldMatrices and their OBJrange/minZ's first:
	int ShadowMapIdx = 1; // Shadow maps for the hangar are always located at index 1
	g_ShadowMapVSCBuffer.OBJrange[ShadowMapIdx] = range;
	g_ShadowMapVSCBuffer.OBJminZ[ShadowMapIdx] = minZ;

	// Clear the Shadow Map DSV
	context->ClearDepthStencilView(resources->_shadowMapDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	// Set the Shadow Map DSV
	context->OMSetRenderTargets(0, 0, resources->_shadowMapDSV.Get());
	Matrix4 S1, S2;
	S1.scale(OPT_TO_METERS, -OPT_TO_METERS, OPT_TO_METERS);
	S2.scale(METERS_TO_OPT, -METERS_TO_OPT, METERS_TO_OPT);

	for (DrawCommand command : _ShadowMapDrawCommands) {
		// Set the mesh buffers
		ID3D11ShaderResourceView* vsSSRV[1] = { command.vertexSRV };
		context->VSSetShaderResources(0, 1, vsSSRV);

		// Set the index and vertex buffers
		_deviceResources->InitVertexBuffer(nullptr, nullptr, nullptr);
		_deviceResources->InitVertexBuffer(&(command.vertexBuffer), &vertexBufferStride, &vertexBufferOffset);
		_deviceResources->InitIndexBuffer(nullptr, true);
		_deviceResources->InitIndexBuffer(command.indexBuffer, true);

		// Compute the LightView (Parallel Projection) Matrix
		// See HangarShadowSceneHook for an explanation of why L looks like this:
		Matrix4 L = _hangarShadowMapRotation * S1 * Matrix4(command.constants.hangarShadowView) * S2;
		g_ShadowMapVSCBuffer.lightWorldMatrix[ShadowMapIdx] = ST * L;
		g_ShadowMapVSCBuffer.light_index = ShadowMapIdx;

		// Set the constants buffer
		context->UpdateSubresource(_constantBuffer, 0, nullptr, &(command.constants), 0, 0);

		// Set the number of triangles
		_trianglesCount = command.trianglesCount;

		// Set the constant buffer for the VS and PS.
		resources->InitVSConstantBufferShadowMap(resources->_shadowMappingVSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);
		// The pixel shader is empty for the shadow map, but the SSAO/SSDO/Deferred PS do use these constants later
		resources->InitPSConstantBufferShadowMap(resources->_shadowMappingPSConstantBuffer.GetAddressOf(), &g_ShadowMapVSCBuffer);

		// Render the Shadow Map
		context->DrawIndexed(_trianglesCount * 3, 0, 0);
	}

	// Copy the shadow map to the right slot in the array
	context->CopySubresourceRegion(resources->_shadowMapArray, D3D11CalcSubresource(0, ShadowMapIdx, 1), 0, 0, 0,
		resources->_shadowMap, D3D11CalcSubresource(0, 0, 1), NULL);

	if (g_bDumpSSAOBuffers) {
		context->CopySubresourceRegion(resources->_shadowMapDebug, D3D11CalcSubresource(0, 0, 1), 0, 0, 0,
			resources->_shadowMapArray, D3D11CalcSubresource(0, ShadowMapIdx, 1), NULL);
		DirectX::SaveDDSTextureToFile(context, resources->_shadowMapDebug, L"c:\\Temp\\_shadowMapHangar.dds");
	}

	RestoreContext();
	_bHangarShadowsRenderedInCurrentFrame = true;
	_ShadowMapDrawCommands.clear();
}

void EffectsRenderer::HangarShadowSceneHook(const SceneCompData* scene)
{
	if (!g_config.EnableSoftHangarShadows) {
		// Jump to the original version of this hook and return: this disables the new effect.
		// There are some artifacts that need to be fixed before going live with this version.
		D3dRenderer::HangarShadowSceneHook(scene);
		return;
	}

	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;
	auto &resources = _deviceResources;

	ComPtr<ID3D11Buffer> oldVSConstantBuffer;
	ComPtr<ID3D11Buffer> oldPSConstantBuffer;
	ComPtr<ID3D11ShaderResourceView> oldVSSRV[3];

	context->VSGetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSGetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSGetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());

	context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
	_deviceResources->InitRasterizerState(_rasterizerState);
	_deviceResources->InitSamplerState(_samplerState.GetAddressOf(), nullptr);

	UpdateTextures(scene);
	UpdateMeshBuffers(scene);
	UpdateVertexAndIndexBuffers(scene);
	UpdateConstantBuffer(scene);

	int ShadowMapIdx = 1;
	Matrix4 W = XwaTransformToMatrix4(scene->WorldViewTransform);
	Matrix4 S1, S2;
	S1.scale(OPT_TO_METERS, -OPT_TO_METERS, OPT_TO_METERS);
	S2.scale(METERS_TO_OPT, -METERS_TO_OPT, METERS_TO_OPT);
	// The transform chain here looks a bit odd, but it makes sense. Here's the explanation.
	// XWA uses:
	//
	//     V = _constants.hangarShadowView * WorldViewTransform * V
	//
	// To transform from the OPT system to the world view and then to the light's point of view
	// in the hangar. But this transform chain only works when transforming from OPT coordinates.
	// Meanwhile, the transform rule in XwaD3DPixelShader looks like this:
	//
	//     output.pos3D = scale(OPT_TO_METERS, -OPT_TO_METERS, OPT_TO_METERS) * transformWorldView * V
	//
	// or:
	//
	//     output.pos3D = S * W * V
	//
	// We need pos3D, because that's what we use for shadow mapping. But we need to apply
	// hangarShadowView before we apply the OPT_TO_METERS scale matrix or the transform won't work.
	// So this is how I solved this problem:
	// Transform V into metric 3D, so that we get a valid pos3D, but then invert the scale matrix
	// so that we get OPT scale back again and then we can apply the hangarShadowView in a valid
	// coordinate system. Finally, apply the scale matrix once more to get metric coordinates and
	// project. Unfortunately, after using hangarShadowView, things will be "upside down" in the
	// shadow map, so we have to rotate things by 180 degrees. In other words, we do this:
	//
	//    V = (RotX180 * S * hangarShadowView * S') * S * W * V
	//
	// where L = (RotX180 * S * hangarShadowView * S') is our light view transform:
	// (BTW, we need to use this same rule in HangarShadowMapVS)
	Matrix4 L = _hangarShadowMapRotation * S1 * Matrix4(_constants.hangarShadowView) * S2;

	// Compute the AABB for the hangar to use later when rendering the shadow map
	auto it = _AABBs.find((int)(scene->MeshVertices));
	if (it != _AABBs.end())
	{
		AABB aabb = it->second;
		aabb.UpdateLimits();
		aabb.TransformLimits(L * S1 * W);
		_hangarShadowAABB.Expand(aabb.Limits);
	}

	// Dump the current scene to an OBJ file
	if (g_bDumpSSAOBuffers && (bHangarDumpOBJEnabled || bD3DDumpOBJEnabled))
		OBJDumpD3dVertices(scene, _hangarShadowMapRotation);

	// The hangar shadow map must be rendered as a post-processing effect. The main reason is that
	// we don't know the size of the AABB for the hangar until we're done rendering it. So, we're
	// going to store the commands to use later.
	DrawCommand command;
	// Save the Vertex SRV
	command.vertexSRV = _lastMeshVerticesView;
	// Save the vertex and index buffers
	command.vertexBuffer = _lastVertexBuffer;
	command.indexBuffer = _lastIndexBuffer;
	command.trianglesCount = _trianglesCount;
	// Save the constants
	command.constants = _constants;
	// Add the command to the list of deferred commands
	_ShadowMapDrawCommands.push_back(command);

	context->VSSetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSSetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());
}

/*
 * This method is called from two places: in Direct3D::Execute() at the beginning of the HUD and
 * in PrimarySurface::Flip() before we start rendering post-proc effects. Any calls placed in this
 * method should be idempotent or they will render the same content twice.
 */
void EffectsRenderer::RenderDeferredDrawCalls()
{
	// All the calls below should be rendered with RendererType_Main
	g_rendererType = RendererType_Main;
	RenderCockpitShadowMap();
	RenderHangarShadowMap();
	RenderLasers();
	RenderTransparency();
}
