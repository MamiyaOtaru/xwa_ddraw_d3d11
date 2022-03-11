/*
 * List of known issues:
 * - Faceted normal maps --> Not an issue with ddraw, OPTs have the wrong normals.
 * - VR
 * - Animations (including Debris)
 * - Animated Explosions (do they even work now?)
 * - Explosion Variants
 * - The Map was semi-fixed apparently as a side-effect of fixing the artifacts in the CMD.
 *   Turns out the map lines were being captured in the Dynamic Cockpit FG buffer, probably
 *   because we didn't detect the miniature being rendered when in map mode.
 *
 * New ideas that might be possible now:
 *
 * - Control the position of ships hypering in or out. Make them *snap* into place
 * - Animate the 3D cockpit. Moving joysticks, targetting computer, etc
 * - Aiming Reticle with lead
 * - Global shadow mapping?
 * - Ray-tracing? Do we have all the unculled, unclipped 3D data now?
 *		Use Intel Embree to build the BVH, one ray per pixel to compute global shadows
 *		outside the cockpit.
 * - Universal head tracking through the tranformWorldView matrix
 * - Parallax mapping: we have the OPT -> Viewspace transform matrix now!
 */
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "XwaD3dRendererHook.h"
#include "EffectsRenderer.h"
#include "SteamVRRenderer.h"

#ifdef _DEBUG
#include "../Debug/XwaD3dVertexShader.h"
#include "../Debug/XwaD3dPixelShader.h"
#include "../Debug/XwaD3dShadowVertexShader.h"
#include "../Debug/XwaD3dShadowPixelShader.h"
#include "../Debug/XwaD3DTechRoomPixelShader.h"
#else
#include "../Release/XwaD3dVertexShader.h"
#include "../Release/XwaD3dPixelShader.h"
#include "../Release/XwaD3dShadowVertexShader.h"
#include "../Release/XwaD3dShadowPixelShader.h"
#include "../Release/XwaD3DTechRoomPixelShader.h"
#endif

#undef LOGGER_DUMP
#define LOGGER_DUMP 0

#if LOGGER_DUMP

std::ofstream g_d3d_file;

void DumpFile(std::string filename)
{
	g_d3d_file.open(filename);
}

void DumpConstants(D3dConstants& constants)
{
	std::ostringstream str;
	str << std::setprecision(30);

	str << "\tConstants" << std::endl;
	str << "\t" << constants.viewportScale[0] << "; " << constants.viewportScale[1] << "; " << constants.viewportScale[2] << "; " << constants.viewportScale[3] << std::endl;
	str << "\t" << constants.projectionValue1 << std::endl;
	str << "\t" << constants.projectionValue2 << std::endl;
	str << "\t" << constants.projectionDeltaX << std::endl;
	str << "\t" << constants.projectionDeltaY << std::endl;

	str << "\t" << constants.transformWorldView[0] << "; " << constants.transformWorldView[1] << "; " << constants.transformWorldView[2] << "; " << constants.transformWorldView[3] << std::endl;
	str << "\t" << constants.transformWorldView[4] << "; " << constants.transformWorldView[5] << "; " << constants.transformWorldView[6] << "; " << constants.transformWorldView[7] << std::endl;
	str << "\t" << constants.transformWorldView[8] << "; " << constants.transformWorldView[9] << "; " << constants.transformWorldView[10] << "; " << constants.transformWorldView[11] << std::endl;
	str << "\t" << constants.transformWorldView[12] << "; " << constants.transformWorldView[13] << "; " << constants.transformWorldView[14] << "; " << constants.transformWorldView[15] << std::endl;

	g_d3d_file << str.str() << std::endl;
}

void DumpVector3(XwaVector3* vertices, int count)
{
	std::ostringstream str;
	str << std::setprecision(30);

	for (int index = 0; index < count; index++)
	{
		XwaVector3 v = vertices[index];

		str << "\tVERT" << index << ":";
		str << "\t( " << v.x << "\t; " << v.y << "\t; " << v.z << " )";
		str << std::endl;
	}

	g_d3d_file << str.str() << std::endl;
}

void DumpTextureVertices(XwaTextureVertex* vertices, int count)
{
	std::ostringstream str;
	str << std::setprecision(30);

	for (int index = 0; index < count; index++)
	{
		XwaTextureVertex v = vertices[index];

		str << "\tTEX" << index << ":";
		str << "\t( " << v.u << "\t; " << v.v << " )";
		str << std::endl;
	}

	g_d3d_file << str.str() << std::endl;
}

void DumpD3dVertices(D3dVertex* vertices, int count)
{
	std::ostringstream str;

	for (int index = 0; index < count; index++)
	{
		D3dVertex v = vertices[index];

		str << "\tTRI" << index << ":";
		str << "\t( " << v.iV << "\t; " << v.iT << " )";
		str << std::endl;
	}

	g_d3d_file << str.str() << std::endl;
}

#endif

bool g_isInRenderLasers = false;
bool g_isInRenderMiniature = false;
bool g_isInRenderHyperspaceLines = false;

bool g_bResetCachedMeshes = false;

RendererType g_rendererType = RendererType_Unknown;

D3dRenderer::D3dRenderer()
{
	_isInitialized = false;
	//_meshBufferInitialCount = 65536; // Not actually used anywhere at all
	_lastMeshVertices = nullptr;
	_lastMeshVerticesView = nullptr;
	_lastMeshVertexNormals = nullptr;
	_lastMeshVertexNormalsView = nullptr;
	_lastMeshTextureVertices = nullptr;
	_lastMeshTextureVerticesView = nullptr;
	_constants = {};
	_viewport = {};

#if LOGGER_DUMP
	DumpFile("ddraw_d3d.txt");
#endif
}

void D3dRenderer::SceneBegin(DeviceResources* deviceResources)
{
	_deviceResources = deviceResources;

	if (!_isInitialized)
	{
		Initialize();
		_isInitialized = true;
	}

	_totalVerticesCount = 0;
	_totalTrianglesCount = 0;

	_lastMeshVertices = nullptr;
	_lastMeshVerticesView = nullptr;
	_lastMeshVertexNormals = nullptr;
	_lastMeshVertexNormalsView = nullptr;
	_lastMeshTextureVertices = nullptr;
	_lastMeshTextureVerticesView = nullptr;

	GetViewport(&_viewport);
	GetViewportScale(_constants.viewportScale);

	if (g_bResetCachedMeshes)
		FlightStart();
}

void D3dRenderer::SceneEnd()
{
	static int PrevD3DExecuteCounter = 0;
	// Reset the mesh cache if we're in the Tech Room every time the draw counter
	// changes. This isn't a perfect fix, because if the draw call count stays
	// the same after switching to a new ship, then we won't reset the meshes,
	// but it helps.
	if (g_bInTechRoom && g_iD3DExecuteCounter != PrevD3DExecuteCounter) {
		PrevD3DExecuteCounter = g_iD3DExecuteCounter;
		FlightStart();
	}
}

void D3dRenderer::FlightStart()
{
	_meshVerticesViews.clear();
	_meshNormalsViews.clear();
	_meshTextureCoordsViews.clear();
	_vertexBuffers.clear();
	_triangleBuffers.clear();
	_vertexCounters.clear();
	_AABBs.clear();
	ClearCachedSRVs();
}

void D3dRenderer::MainSceneHook(const SceneCompData* scene)
{
	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;

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
	}
	else
	{
		_deviceResources->InitBlendState(_transparentBlendState, nullptr);
		_deviceResources->InitDepthStencilState(_transparentDepthState, nullptr);
	}

	_deviceResources->InitViewport(&_viewport);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);
	_deviceResources->InitVertexShader(_vertexShader);
	_deviceResources->InitPixelShader(_pixelShader);

	UpdateTextures(scene);
	UpdateMeshBuffers(scene);
	UpdateVertexAndIndexBuffers(scene);
	UpdateConstantBuffer(scene);
	RenderScene();

	context->VSSetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSSetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());

#if LOGGER_DUMP
	DumpConstants(_constants);
	DumpVector3(scene->MeshVertices, *(int*)((int)scene->MeshVertices - 8));
	DumpTextureVertices(scene->MeshTextureVertices, *(int*)((int)scene->MeshTextureVertices - 8));
	DumpD3dVertices(_vertices.data(), _verticesCount);
#endif
}

void D3dRenderer::HangarShadowSceneHook(const SceneCompData* scene)
{
	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;

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

	_deviceResources->InitBlendState(_solidBlendState, nullptr);
	_deviceResources->InitDepthStencilState(_solidDepthState, nullptr, 0U);

	_deviceResources->InitViewport(&_viewport);
	_deviceResources->InitTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceResources->InitInputLayout(_inputLayout);
	_deviceResources->InitVertexShader(_shadowVertexShader);
	_deviceResources->InitPixelShader(_shadowPixelShader);

	UpdateTextures(scene);
	UpdateMeshBuffers(scene);
	UpdateVertexAndIndexBuffers(scene);
	UpdateConstantBuffer(scene);
	RenderScene();

	context->VSSetConstantBuffers(0, 1, oldVSConstantBuffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, oldPSConstantBuffer.GetAddressOf());
	context->VSSetShaderResources(0, 3, oldVSSRV[0].GetAddressOf());
}

void D3dRenderer::UpdateTextures(const SceneCompData* scene)
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

	if (g_isInRenderLasers)
	{
		_constants.renderType = 2;
	}

	if (isProjectile)
	{
		_constants.renderType = 2;
	}

	if (scene->D3DInfo != nullptr)
	{
		surface = scene->D3DInfo->ColorMap[0];

		if (scene->D3DInfo->LightMap[0] != nullptr)
		{
			surface2 = scene->D3DInfo->LightMap[0];
			_constants.renderTypeIllum = 1;
		}
	}
	else
	{
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
}

void D3dRenderer::UpdateMeshBuffers(const SceneCompData* scene)
{
	ID3D11Device* device = _deviceResources->_d3dDevice;
	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;

	XwaVector3* vertices = scene->MeshVertices;
	XwaVector3* normals = scene->MeshVertexNormals;
	XwaTextureVertex* textureCoords = scene->MeshTextureVertices;

	if (vertices != _lastMeshVertices)
	{
		_lastMeshVertices = vertices;
		_currentFaceIndex = 0;

		auto it = _meshVerticesViews.find((int)vertices);

		if (it != _meshVerticesViews.end())
		{
			_lastMeshVerticesView = it->second;
		}
		else
		{
			int verticesCount = *(int*)((int)vertices - 8);

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = vertices;
			initialData.SysMemPitch = 0;
			initialData.SysMemSlicePitch = 0;

			ComPtr<ID3D11Buffer> meshVerticesBuffer;
			device->CreateBuffer(&CD3D11_BUFFER_DESC(verticesCount * sizeof(XwaVector3), D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE), &initialData, &meshVerticesBuffer);

			ID3D11ShaderResourceView* meshVerticesView;
			device->CreateShaderResourceView(meshVerticesBuffer, &CD3D11_SHADER_RESOURCE_VIEW_DESC(meshVerticesBuffer, DXGI_FORMAT_R32G32B32_FLOAT, 0, verticesCount), &meshVerticesView);

			AABB aabb;
			aabb.SetInfinity();
			for (int i = 0; i < verticesCount; i++)
				aabb.Expand(vertices[i]);

			_meshVerticesViews.insert(std::make_pair((int)vertices, meshVerticesView));
			_AABBs.insert(std::make_pair((int)vertices, aabb));
			_lastMeshVerticesView = meshVerticesView;
		}
	}

	if (normals != _lastMeshVertexNormals)
	{
		_lastMeshVertexNormals = normals;

		auto it = _meshNormalsViews.find((int)normals);

		if (it != _meshNormalsViews.end())
		{
			_lastMeshVertexNormalsView = it->second;
		}
		else
		{
			int normalsCount = *(int*)((int)normals - 8);

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = normals;
			initialData.SysMemPitch = 0;
			initialData.SysMemSlicePitch = 0;

			ComPtr<ID3D11Buffer> meshNormalsBuffer;
			device->CreateBuffer(&CD3D11_BUFFER_DESC(normalsCount * sizeof(XwaVector3), D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE), &initialData, &meshNormalsBuffer);

			ID3D11ShaderResourceView* meshNormalsView;
			device->CreateShaderResourceView(meshNormalsBuffer, &CD3D11_SHADER_RESOURCE_VIEW_DESC(meshNormalsBuffer, DXGI_FORMAT_R32G32B32_FLOAT, 0, normalsCount), &meshNormalsView);

			_meshNormalsViews.insert(std::make_pair((int)normals, meshNormalsView));
			_lastMeshVertexNormalsView = meshNormalsView;
		}
	}

	if (textureCoords != _lastMeshTextureVertices)
	{
		_lastMeshTextureVertices = textureCoords;

		auto it = _meshTextureCoordsViews.find((int)textureCoords);

		if (it != _meshTextureCoordsViews.end())
		{
			_lastMeshTextureVerticesView = it->second;
		}
		else
		{
			int textureCoordsCount = *(int*)((int)textureCoords - 8);

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = textureCoords;
			initialData.SysMemPitch = 0;
			initialData.SysMemSlicePitch = 0;

			ComPtr<ID3D11Buffer> meshTextureCoordsBuffer;
			device->CreateBuffer(&CD3D11_BUFFER_DESC(textureCoordsCount * sizeof(XwaTextureVertex), D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE), &initialData, &meshTextureCoordsBuffer);

			ID3D11ShaderResourceView* meshTextureCoordsView;
			device->CreateShaderResourceView(meshTextureCoordsBuffer, &CD3D11_SHADER_RESOURCE_VIEW_DESC(meshTextureCoordsBuffer, DXGI_FORMAT_R32G32_FLOAT, 0, textureCoordsCount), &meshTextureCoordsView);

			_meshTextureCoordsViews.insert(std::make_pair((int)textureCoords, meshTextureCoordsView));
			_lastMeshTextureVerticesView = meshTextureCoordsView;
		}
	}

	ID3D11ShaderResourceView* vsSSRV[3] = { _lastMeshVerticesView, _lastMeshVertexNormalsView, _lastMeshTextureVerticesView };
	context->VSSetShaderResources(0, 3, vsSSRV);
}

void D3dRenderer::ResizeDataVector(const SceneCompData* scene)
{
	size_t verticesRequiredSize = scene->FacesCount * 4;
	size_t trianglesRequiredSize = scene->FacesCount * 2;

	if (_vertices.capacity() < verticesRequiredSize)
	{
		_vertices.reserve(verticesRequiredSize * 2);
	}

	if (_triangles.capacity() < trianglesRequiredSize)
	{
		_triangles.reserve(trianglesRequiredSize * 2);
	}
}

void D3dRenderer::CreateDataScene(const SceneCompData* scene)
{
	const unsigned char ShipCategory_PlayerProjectile = 6;
	const unsigned char ShipCategory_OtherProjectile = 7;
	unsigned char category = scene->pObject->ShipCategory;
	int isProjectile = (category == ShipCategory_PlayerProjectile || category == ShipCategory_OtherProjectile) ? 1 : 0;

	D3dVertex* v = _vertices.data();
	D3dTriangle* t = _triangles.data();
	int verticesIndex = 0;
	_verticesCount = 0;
	_trianglesCount = 0;

	for (int faceIndex = 0; faceIndex < scene->FacesCount; faceIndex++)
	{
		OptFaceDataNode_01_Data_Indices& faceData = scene->FaceIndices[faceIndex];
		int edgesCount = faceData.Edge[3] == -1 ? 3 : 4;

		for (int vertexIndex = 0; vertexIndex < edgesCount; vertexIndex++)
		{
			v->iV = faceData.Vertex[vertexIndex];
			v->iN = faceData.VertexNormal[vertexIndex];
			v->iT = faceData.TextureVertex[vertexIndex];
			//v->c = isProjectile;
			v->c = 0;
			v++;
		}

		_verticesCount += edgesCount;

		// This converts quads into 2 tris if necessary
		for (int edge = 2; edge < edgesCount; edge++)
		{
			t->v1 = verticesIndex;
			t->v2 = verticesIndex + edge - 1;
			t->v3 = verticesIndex + edge;
			t++;
		}

		verticesIndex += edgesCount;
		_trianglesCount += edgesCount - 2;
	}

	_totalVerticesCount += _verticesCount;
	_totalTrianglesCount += _trianglesCount;
}

void D3dRenderer::UpdateVertexAndIndexBuffers(const SceneCompData* scene)
{
	ID3D11Device* device = _deviceResources->_d3dDevice;
	OptFaceDataNode_01_Data_Indices* faceData = scene->FaceIndices;

	ID3D11Buffer* vertexBuffer;
	ID3D11Buffer* indexBuffer;
	int verticesCount;
	int trianglesCount;

	auto itVertex = _vertexBuffers.find((int)faceData);
	auto itTriangle = _triangleBuffers.find((int)faceData);
	auto itCounters = _vertexCounters.find((int)faceData);

	if (itVertex != _vertexBuffers.end())
	{
		vertexBuffer = itVertex->second;
		indexBuffer = itTriangle->second;
		verticesCount = itCounters->second.first;
		trianglesCount = itCounters->second.second;
	}
	else
	{
		ResizeDataVector(scene);
		CreateDataScene(scene);

		D3D11_SUBRESOURCE_DATA initialData;
		initialData.SysMemPitch = 0;
		initialData.SysMemSlicePitch = 0;

		initialData.pSysMem = _vertices.data();
		device->CreateBuffer(&CD3D11_BUFFER_DESC(_verticesCount * sizeof(D3dVertex), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE), &initialData, &vertexBuffer);

		initialData.pSysMem = _triangles.data();
		device->CreateBuffer(&CD3D11_BUFFER_DESC(_trianglesCount * sizeof(D3dTriangle), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE), &initialData, &indexBuffer);

		verticesCount = _verticesCount;
		trianglesCount = _trianglesCount;

		_vertexBuffers.insert(std::make_pair((int)faceData, vertexBuffer));
		_triangleBuffers.insert(std::make_pair((int)faceData, indexBuffer));
		_vertexCounters.insert(std::make_pair((int)faceData, std::make_pair(_verticesCount, _trianglesCount)));
	}

	UINT vertexBufferStride = sizeof(D3dVertex);
	UINT vertexBufferOffset = 0;
	_deviceResources->InitVertexBuffer(nullptr, nullptr, nullptr);
	_deviceResources->InitVertexBuffer(&vertexBuffer, &vertexBufferStride, &vertexBufferOffset);
	_deviceResources->InitIndexBuffer(nullptr, true);
	_deviceResources->InitIndexBuffer(indexBuffer, true);

	_lastVertexBuffer = vertexBuffer;
	_lastIndexBuffer = indexBuffer;
	_verticesCount = verticesCount;
	_trianglesCount = trianglesCount;

	unsigned int& s_SceneNumVerts = *(unsigned int*)0x009EAA00;
	unsigned int& s_SceneNumTris = *(unsigned int*)0x009EA96C;
	unsigned int& s_SceneNumTexChanges = *(unsigned int*)0x009EA984;
	unsigned int& s_SceneNumStateChanges = *(unsigned int*)0x009EA980;

	s_SceneNumVerts += verticesCount;
	s_SceneNumTris += trianglesCount;
	s_SceneNumStateChanges += 1;
}

void D3dRenderer::UpdateConstantBuffer(const SceneCompData* scene)
{
	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;

	switch (g_rendererType)
	{
	case RendererType_Main:
	default:
		_constants.projectionValue1 = *(float*)0x08B94CC; // Znear
		_constants.projectionValue2 = *(float*)0x05B46B4; // Zfar
		_constants.projectionDeltaX = *(float*)0x08C1600 + *(float*)0x0686ACC;
		_constants.projectionDeltaY = *(float*)0x080ACF8 + *(float*)0x07B33C0 + *(float*)0x064D1AC;
		_constants.floorLevel = 0.0f;
		_constants.cameraPositionX = (float)(*(int*)(0x08B94E0 + *(int*)0x08C1CC8 * 0xBCF + 0xB48 + 0x00));
		_constants.cameraPositionY = (float)(*(int*)(0x08B94E0 + *(int*)0x08C1CC8 * 0xBCF + 0xB48 + 0x04));
		_constants.hangarShadowAccStartEnd = 0.0f;
		break;

	case RendererType_Shadow:
		_constants.projectionValue1 = *(float*)0x08B94CC;
		_constants.projectionValue2 = *(float*)0x05B46B4;
		_constants.projectionDeltaX = (float)(*(short*)0x08052B8 / 2) + *(float*)0x0686ACC;
		_constants.projectionDeltaY = (float)(*(short*)0x07B33BC / 2 + *(int*)0x080811C) + *(float*)0x064D1AC;
		_constants.floorLevel = *(float*)0x0686B34 - -1.5f - (float)(*(int*)(0x08B94E0 + *(int*)0x08C1CC8 * 0xBCF + 0xB48 + 0x08));
		_constants.cameraPositionX = (float)(*(int*)(0x08B94E0 + *(int*)0x08C1CC8 * 0xBCF + 0xB48 + 0x00));
		_constants.cameraPositionY = (float)(*(int*)(0x08B94E0 + *(int*)0x08C1CC8 * 0xBCF + 0xB48 + 0x04));
		_constants.hangarShadowAccStartEnd = *(short*)0x0686B38 != 0 ? 1.0f : 0.0f;
		_constants.sx1 = *(float*)0x0662814;
		_constants.sx2 = *(float*)0x064D1A4;
		_constants.sy1 = *(float*)0x06626F4;
		_constants.sy2 = *(float*)0x063D194;
		break;
	}

	// This matrix is probably used to transform the objects in the hangar so that
	// they lie "top-down". Shadows seem to be produced by flattening the objects
	// and then projecting them to the camera plane
	_constants.hangarShadowView[0] = *(float*)0x007B4BEC;
	_constants.hangarShadowView[1] = *(float*)0x007B6FF8;
	_constants.hangarShadowView[2] = *(float*)0x007B33DC;
	_constants.hangarShadowView[3] = 0.0f;
	_constants.hangarShadowView[4] = *(float*)0x007B4BE8;
	_constants.hangarShadowView[5] = *(float*)0x007B6FF0;
	_constants.hangarShadowView[6] = *(float*)0x007B33D8;
	_constants.hangarShadowView[7] = 0.0f;
	_constants.hangarShadowView[8] = *(float*)0x007B4BF4;
	_constants.hangarShadowView[9] = *(float*)0x007B33D4;
	_constants.hangarShadowView[10] = *(float*)0x007B4BE4;
	_constants.hangarShadowView[11] = 0.0f;
	_constants.hangarShadowView[12] = 0.0f;
	_constants.hangarShadowView[13] = 0.0f;
	_constants.hangarShadowView[14] = 0.0f;
	_constants.hangarShadowView[15] = 1.0f;

	_constants.transformWorldView[0] = scene->WorldViewTransform.Rotation._11;
	_constants.transformWorldView[1] = scene->WorldViewTransform.Rotation._21;
	_constants.transformWorldView[2] = scene->WorldViewTransform.Rotation._31;
	_constants.transformWorldView[3] = scene->WorldViewTransform.Position.x;
	_constants.transformWorldView[4] = scene->WorldViewTransform.Rotation._12;
	_constants.transformWorldView[5] = scene->WorldViewTransform.Rotation._22;
	_constants.transformWorldView[6] = scene->WorldViewTransform.Rotation._32;
	_constants.transformWorldView[7] = scene->WorldViewTransform.Position.y;
	_constants.transformWorldView[8] = scene->WorldViewTransform.Rotation._13;
	_constants.transformWorldView[9] = scene->WorldViewTransform.Rotation._23;
	_constants.transformWorldView[10] = scene->WorldViewTransform.Rotation._33;
	_constants.transformWorldView[11] = scene->WorldViewTransform.Position.z;
	_constants.transformWorldView[12] = 0.0f;
	_constants.transformWorldView[13] = 0.0f;
	_constants.transformWorldView[14] = 0.0f;
	_constants.transformWorldView[15] = 1.0f;

	int s_XwaGlobalLightsCount = *(int*)0x00782848;
	XwaGlobalLight* s_XwaGlobalLights = (XwaGlobalLight*)0x007D4FA0;
	int s_XwaLocalLightsCount = *(int*)0x00782844;
	XwaLocalLight* s_XwaLocalLights = (XwaLocalLight*)0x008D42C0;

	_constants.globalLightsCount = s_XwaGlobalLightsCount;

	for (int i = 0; i < 8; i++)
	{
		if (!g_bInTechRoom) {
			_constants.globalLights[i].direction[0] = s_XwaGlobalLights[i].DirectionX;
			_constants.globalLights[i].direction[1] = s_XwaGlobalLights[i].DirectionY;
			_constants.globalLights[i].direction[2] = s_XwaGlobalLights[i].DirectionZ;
			_constants.globalLights[i].direction[3] = 1.0f;
		}
		else {
			// In the Tech Room, global lights are "attached" to the ship, so that they rotate
			// along with it. I don't think that's terribly useful, so I'm inverting the
			// World transform to fix the lights instead.
			Vector4 L = Vector4(s_XwaGlobalLights[i].DirectionX, s_XwaGlobalLights[i].DirectionY, s_XwaGlobalLights[i].DirectionZ, 0.0f);
			Matrix4 W = Matrix4(_constants.transformWorldView);
			W = W.transpose(); // This is effectively an inverse of W
			L = W * L;
			// The coordinate system in the XwaD3DTechRoomPixelShader has YZ inverted, let's do it here as well:
			_constants.globalLights[i].direction[0] =  L.x;
			_constants.globalLights[i].direction[1] = -L.y;
			_constants.globalLights[i].direction[2] = -L.z;
			_constants.globalLights[i].direction[3] =  0.0f;
		}

		_constants.globalLights[i].color[0] = s_XwaGlobalLights[i].ColorR;
		_constants.globalLights[i].color[1] = s_XwaGlobalLights[i].ColorG;
		_constants.globalLights[i].color[2] = s_XwaGlobalLights[i].ColorB;
		_constants.globalLights[i].color[3] = s_XwaGlobalLights[i].Intensity;
	}

	_constants.localLightsCount = s_XwaLocalLightsCount;

	for (int i = 0; i < 8; i++)
	{
		_constants.localLights[i].position[0] = s_XwaLocalLights[i].fPositionX;
		_constants.localLights[i].position[1] = s_XwaLocalLights[i].fPositionY;
		_constants.localLights[i].position[2] = s_XwaLocalLights[i].fPositionZ;
		_constants.localLights[i].position[3] = 1.0f;

		_constants.localLights[i].color[0] = s_XwaLocalLights[i].ColorR;
		_constants.localLights[i].color[1] = s_XwaLocalLights[i].ColorG;
		_constants.localLights[i].color[2] = s_XwaLocalLights[i].ColorB;
		_constants.localLights[i].color[3] = s_XwaLocalLights[i].m18;
	}

	context->UpdateSubresource(_constantBuffer, 0, nullptr, &_constants, 0, 0);
}

void D3dRenderer::RenderScene()
{
	if (_deviceResources->_displayWidth == 0 || _deviceResources->_displayHeight == 0)
	{
		return;
	}

	ID3D11DeviceContext* context = _deviceResources->_d3dDeviceContext;

	unsigned short scissorLeft = *(unsigned short*)0x07D5244;
	unsigned short scissorTop = *(unsigned short*)0x07CA354;
	unsigned short scissorWidth = *(unsigned short*)0x08052B8;
	unsigned short scissorHeight = *(unsigned short*)0x07B33BC;
	float scaleX = _viewport.Width / _deviceResources->_displayWidth;
	float scaleY = _viewport.Height / _deviceResources->_displayHeight;
	D3D11_RECT scissor{};
	scissor.left = (LONG)(_viewport.TopLeftX + scissorLeft * scaleX + 0.5f);
	scissor.top = (LONG)(_viewport.TopLeftY + scissorTop * scaleY + 0.5f);
	scissor.right = scissor.left + (LONG)(scissorWidth * scaleX + 0.5f);
	scissor.bottom = scissor.top + (LONG)(scissorHeight * scaleY + 0.5f);
	_deviceResources->InitScissorRect(&scissor);

	context->DrawIndexed(_trianglesCount * 3, 0, 0);
}

void D3dRenderer::RenderDeferredDrawCalls()
{

}

void D3dRenderer::BuildGlowMarks(SceneCompData* scene)
{
	_glowMarksVertices.clear();
	_glowMarksTriangles.clear();

	const auto XwaD3dTextureCacheUpdateOrAdd = (void(*)(XwaTextureSurface*))0x00597784;
	const auto XwaVector3Transform = (void(*)(XwaVector3*, const XwaMatrix3x3*))0x00439B30;
	const auto L004E8110 = (void(*)(SceneCompData*, XwaKnockoutData*))0x004E8110;

	const XwaObject* s_XwaObjects = *(XwaObject**)0x007B33C4;
	const XwaObject3D* s_XwaObjects3D = *(XwaObject3D**)0x00782854;
	const int s_XwaIsInConcourse = *(int*)0x005FFD9C;

	scene->GlowMarksCount = 0;

	if (s_XwaIsInConcourse == 0)
	{
		int objectIndex = scene->pObject - s_XwaObjects;
		XwaKnockoutData* knockout = s_XwaObjects3D[objectIndex].pKnockoutData;

		if (knockout != nullptr)
		{
			L004E8110(scene, knockout);
		}
	}

	for (int glowMarkIndex = 0; glowMarkIndex < scene->GlowMarksCount; glowMarkIndex++)
	{
		XwaGlowMark* glowMark = scene->GlowMarks[glowMarkIndex];

		for (int glowMarkItemIndex = 0; glowMarkItemIndex < glowMark->Count; glowMarkItemIndex++)
		{
			XwaGlowMarkItem* glowMarkItem = &glowMark->Items[glowMarkItemIndex];

			if (glowMarkItem->pTextureSurface == nullptr)
			{
				continue;
			}

			XwaD3dTextureCacheUpdateOrAdd(glowMarkItem->pTextureSurface);

			int baseFaceIndex = _currentFaceIndex;

			for (int faceIndex = 0; faceIndex < scene->FacesCount; faceIndex++)
			{
				unsigned short faceOn = glowMark->FaceOn[baseFaceIndex + faceIndex];

				if (faceOn == 0)
				{
					continue;
				}

				OptFaceDataNode_01_Data_Indices& faceData = scene->FaceIndices[faceIndex];
				int edgesCount = faceData.Edge[3] == -1 ? 3 : 4;
				int verticesIndex = _glowMarksVertices.size();

				for (int vertexIndex = 0; vertexIndex < edgesCount; vertexIndex++)
				{
					int iV = faceData.Vertex[vertexIndex];
					int iN = faceData.VertexNormal[vertexIndex];
					int iT = faceData.TextureVertex[vertexIndex];

					XwaD3dVertex v;
					v.x = scene->MeshVertices[iV].x;
					v.y = scene->MeshVertices[iV].y;
					v.z = scene->MeshVertices[iV].z;
					v.rhw = 0;
					v.color = glowMarkItem->Color;
					v.specular = 0;
					v.tu = glowMark->UVArray[iV].u * glowMarkItem->Size + 0.5f;
					v.tv = glowMark->UVArray[iV].v * glowMarkItem->Size + 0.5f;

					XwaVector3Transform((XwaVector3*)&v.x, &scene->WorldViewTransform.Rotation);
					v.x += scene->WorldViewTransform.Position.x;
					v.y += scene->WorldViewTransform.Position.y;
					v.z += scene->WorldViewTransform.Position.z;
					v.z += g_fGlowMarkZOfs;

					float st0 = _constants.projectionValue1 / v.z;
					v.x = v.x * st0 + _constants.projectionDeltaX;
					v.y = v.y * st0 + _constants.projectionDeltaY;
					v.z = (st0 * _constants.projectionValue2) / (abs(st0) * _constants.projectionValue2 + _constants.projectionValue1);
					v.rhw = st0;

					_glowMarksVertices.push_back(v);
				}

				for (int edge = 2; edge < edgesCount; edge++)
				{
					XwaD3dTriangle t;

					t.v1 = verticesIndex;
					t.v2 = verticesIndex + edge - 1;
					t.v3 = verticesIndex + edge;
					t.RenderStatesFlags = *(unsigned int*)0x06626F0;
					t.pTexture = &glowMarkItem->pTextureSurface->D3dTexture;

					_glowMarksTriangles.push_back(t);
				}
			}
		}
	}

	_currentFaceIndex += scene->FacesCount;
}

void D3dRenderer::RenderGlowMarks()
{
	if (_glowMarksTriangles.size() == 0)
	{
		return;
	}

	if (!g_bDisplayGlowMarks)
		return;

	const auto XwaD3dExecuteBufferLock = (char(*)())0x00595006;
	const auto XwaD3dExecuteBufferAddVertices = (char(*)(void*, int))0x00595095;
	const auto XwaD3dExecuteBufferProcessVertices = (char(*)())0x00595106;
	const auto XwaD3dExecuteBufferAddTriangles = (char(*)(void*, int))0x00595191;
	const auto XwaD3dExecuteBufferUnlockAndExecute = (char(*)())0x005954D6;

	XwaD3dExecuteBufferLock();
	XwaD3dExecuteBufferAddVertices(_glowMarksVertices.data(), _glowMarksVertices.size());
	XwaD3dExecuteBufferProcessVertices();
	XwaD3dExecuteBufferAddTriangles(_glowMarksTriangles.data(), _glowMarksTriangles.size());
	XwaD3dExecuteBufferUnlockAndExecute();

	_glowMarksVertices.clear();
	_glowMarksTriangles.clear();
}

void D3dRenderer::Initialize()
{
	if (_deviceResources->_d3dFeatureLevel < D3D_FEATURE_LEVEL_10_0)
	{
		MessageBox(nullptr, "The D3d renderer hook requires a graphic card that supports D3D_FEATURE_LEVEL_10_0.", "X-Wing Alliance DDraw", MB_ICONWARNING);
		return;
	}

	CreateConstantBuffer();
	CreateStates();
	CreateShaders();
}

void D3dRenderer::CreateConstantBuffer()
{
	ID3D11Device* device = _deviceResources->_d3dDevice;

	// constant buffer
	device->CreateBuffer(&CD3D11_BUFFER_DESC(sizeof(D3dConstants), D3D11_BIND_CONSTANT_BUFFER), nullptr, &_constantBuffer);
}

void D3dRenderer::CreateStates()
{
	ID3D11Device* device = _deviceResources->_d3dDevice;

	D3D11_RASTERIZER_DESC rsDesc{};
	rsDesc.FillMode = g_config.WireframeFillMode ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
	
	// Original settings by Jeremy: no culling.
	// Turns out that some people have reported gaps in the cockpits when culling is on. So, for now
	// I'm re-enabling no culling as the default setting. This can now be toggled in DDraw.cfg.
	if (!g_config.CullBackFaces) {
		rsDesc.CullMode = D3D11_CULL_NONE;
		rsDesc.FrontCounterClockwise = FALSE;
	}
	else {
		// New settings: let's cull back faces!
		rsDesc.CullMode = D3D11_CULL_BACK;
		rsDesc.FrontCounterClockwise = TRUE;
	}

	rsDesc.DepthBias = 0;
	rsDesc.DepthBiasClamp = 0.0f;
	rsDesc.SlopeScaledDepthBias = 0.0f;
	rsDesc.DepthClipEnable = TRUE;
	rsDesc.ScissorEnable = TRUE;
	rsDesc.MultisampleEnable = FALSE;
	rsDesc.AntialiasedLineEnable = FALSE;
	device->CreateRasterizerState(&rsDesc, &_rasterizerState);

	rsDesc.FillMode = D3D11_FILL_WIREFRAME;
	device->CreateRasterizerState(&rsDesc, &_rasterizerStateWireframe);

	D3D11_SAMPLER_DESC samplerDesc;
	samplerDesc.Filter = _deviceResources->_useAnisotropy ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MaxAnisotropy = _deviceResources->_useAnisotropy ? _deviceResources->GetMaxAnisotropy() : 1;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = FLT_MAX;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.BorderColor[0] = 0.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;
	device->CreateSamplerState(&samplerDesc, &_samplerState);

	D3D11_BLEND_DESC solidBlendDesc{};
	solidBlendDesc.AlphaToCoverageEnable = FALSE;
	solidBlendDesc.IndependentBlendEnable = FALSE;
	solidBlendDesc.RenderTarget[0].BlendEnable = FALSE;
	solidBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	solidBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	solidBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	solidBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	solidBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	solidBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	solidBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device->CreateBlendState(&solidBlendDesc, &_solidBlendState);

	D3D11_DEPTH_STENCIL_DESC solidDepthDesc{};
	solidDepthDesc.DepthEnable = TRUE;
	solidDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	solidDepthDesc.DepthFunc = D3D11_COMPARISON_GREATER;
	solidDepthDesc.StencilEnable = FALSE;
	device->CreateDepthStencilState(&solidDepthDesc, &_solidDepthState);

	D3D11_BLEND_DESC transparentBlendDesc{};
	transparentBlendDesc.AlphaToCoverageEnable = FALSE;
	transparentBlendDesc.IndependentBlendEnable = FALSE;
	transparentBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	transparentBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	transparentBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	transparentBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	transparentBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	transparentBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	transparentBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	transparentBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device->CreateBlendState(&transparentBlendDesc, &_transparentBlendState);

	D3D11_DEPTH_STENCIL_DESC transparentDepthDesc{};
	transparentDepthDesc.DepthEnable = TRUE;
	transparentDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	transparentDepthDesc.DepthFunc = D3D11_COMPARISON_GREATER;
	transparentDepthDesc.StencilEnable = FALSE;
	device->CreateDepthStencilState(&transparentDepthDesc, &_transparentDepthState);
}

void D3dRenderer::CreateShaders()
{
	ID3D11Device* device = _deviceResources->_d3dDevice;

	device->CreateVertexShader(g_XwaD3dVertexShader, sizeof(g_XwaD3dVertexShader), nullptr, &_vertexShader);

	const D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	device->CreateInputLayout(vertexLayoutDesc, ARRAYSIZE(vertexLayoutDesc), g_XwaD3dVertexShader, sizeof(g_XwaD3dVertexShader), &_inputLayout);

	device->CreatePixelShader(g_XwaD3dPixelShader, sizeof(g_XwaD3dPixelShader), nullptr, &_pixelShader);
	device->CreatePixelShader(g_XwaD3DTechRoomPixelShader, sizeof(g_XwaD3DTechRoomPixelShader), nullptr, &_techRoomPixelShader);

	device->CreateVertexShader(g_XwaD3dShadowVertexShader, sizeof(g_XwaD3dShadowVertexShader), nullptr, &_shadowVertexShader);
	device->CreatePixelShader(g_XwaD3dShadowPixelShader, sizeof(g_XwaD3dShadowPixelShader), nullptr, &_shadowPixelShader);
}

void D3dRenderer::GetViewport(D3D11_VIEWPORT* viewport)
{
	UINT w;
	UINT h;

	if (g_config.AspectRatioPreserved)
	{
		if (_deviceResources->_backbufferHeight * _deviceResources->_displayWidth <= _deviceResources->_backbufferWidth * _deviceResources->_displayHeight)
		{
			w = _deviceResources->_backbufferHeight * _deviceResources->_displayWidth / _deviceResources->_displayHeight;
			h = _deviceResources->_backbufferHeight;
		}
		else
		{
			w = _deviceResources->_backbufferWidth;
			h = _deviceResources->_backbufferWidth * _deviceResources->_displayHeight / _deviceResources->_displayWidth;
		}
	}
	else
	{
		w = _deviceResources->_backbufferWidth;
		h = _deviceResources->_backbufferHeight;
	}

	UINT left = (_deviceResources->_backbufferWidth - w) / 2;
	UINT top = (_deviceResources->_backbufferHeight - h) / 2;

	viewport->TopLeftX = (float)left;
	viewport->TopLeftY = (float)top;
	viewport->Width = (float)w;
	viewport->Height = (float)h;
	viewport->MinDepth = D3D11_MIN_DEPTH;
	viewport->MaxDepth = D3D11_MAX_DEPTH;
}

void D3dRenderer::GetViewportScale(float* viewportScale)
{
	float scale;

	if (_deviceResources->_frontbufferSurface == nullptr)
	{
		scale = 1.0f;
	}
	else
	{
		if (_deviceResources->_backbufferHeight * _deviceResources->_displayWidth <= _deviceResources->_backbufferWidth * _deviceResources->_displayHeight)
		{
			scale = (float)_deviceResources->_backbufferHeight / (float)_deviceResources->_displayHeight;
		}
		else
		{
			scale = (float)_deviceResources->_backbufferWidth / (float)_deviceResources->_displayWidth;
		}

		scale *= g_config.Concourse3DScale;
	}

	viewportScale[0] = 2.0f / (float)_deviceResources->_displayWidth;
	viewportScale[1] = -2.0f / (float)_deviceResources->_displayHeight;
	viewportScale[2] = scale;
	viewportScale[3] = 0.0f;
}

void D3dRenderer::SetRenderTypeIllum(int type)
{
	auto context = _deviceResources->_d3dDeviceContext;
	_constants.renderTypeIllum = type;
	context->UpdateSubresource(_constantBuffer, 0, nullptr, &_constants, 0, 0);
}

D3dRenderer g_xwa_d3d_renderer;

// TODO: Select the appropriate renderer depending on the current config
//#define ORIGINAL_D3D_RENDERER_SHADERS
#ifdef ORIGINAL_D3D_RENDERER_SHADERS
static D3dRenderer &g_current_renderer = g_xwa_d3d_renderer;
#endif

void RenderDeferredDrawCalls() {
	g_current_renderer->RenderDeferredDrawCalls();
}

//************************************************************************
// Hooks
//************************************************************************

void D3dRendererSceneBegin(DeviceResources* deviceResources)
{
	if (!g_config.D3dRendererHookEnabled)
	{
		return;
	}

	g_current_renderer->SceneBegin(deviceResources);
}

void D3dRendererSceneEnd()
{
	if (!g_config.D3dRendererHookEnabled)
	{
		return;
	}

	g_current_renderer->SceneEnd();
}

void D3dRendererFlightStart()
{
	if (!g_config.D3dRendererHookEnabled)
	{
		return;
	}

	g_current_renderer->FlightStart();
}

void D3dRenderLasersHook(int A4)
{
	const auto L0042BBA0 = (void(*)(int))0x0042BBA0;

	g_isInRenderLasers = true;
	L0042BBA0(A4);
	g_isInRenderLasers = false;
}

void D3dRenderMiniatureHook(int A4, int A8, int AC, int A10, int A14)
{
	const auto L00478490 = (void(*)(int, int, int, int, int))0x00478490;

	g_isInRenderMiniature = true;
	L00478490(A4, A8, AC, A10, A14);
	g_isInRenderMiniature = false;
}

void D3dRenderHyperspaceLinesHook(int A4)
{
	const auto L00480A80 = (void(*)(int))0x00480A80;

	g_isInRenderHyperspaceLines = true;
	L00480A80(A4);
	g_isInRenderHyperspaceLines = false;
}

void D3dRendererMainHook(SceneCompData* scene)
{
	if (*(int*)0x06628E0 != 0)
	{
		const auto XwaD3dExecuteBufferLock = (char(*)())0x00595006;
		const auto XwaD3dExecuteBufferAddVertices = (char(*)(void*, int))0x00595095;
		const auto XwaD3dExecuteBufferProcessVertices = (char(*)())0x00595106;
		const auto XwaD3dExecuteBufferAddTriangles = (char(*)(void*, int))0x00595191;
		const auto XwaD3dExecuteBufferUnlockAndExecute = (char(*)())0x005954D6;

		XwaD3dExecuteBufferLock();
		XwaD3dExecuteBufferAddVertices(*(void**)0x064D1A8, *(int*)0x06628E0);
		XwaD3dExecuteBufferProcessVertices();
		XwaD3dExecuteBufferAddTriangles(*(void**)0x066283C, *(int*)0x0686ABC);
		XwaD3dExecuteBufferUnlockAndExecute();

		*(int*)0x0686ABC = 0;
		*(int*)0x06628E0 = 0;
	}

	const auto L00480370 = (void(*)(const SceneCompData* scene))0x00480370;

	if (g_isInRenderHyperspaceLines)
	{
		// The hyperspace effect is super slow for some reason. Let's disable it if we're rendering the new
		// version
		if (!g_bReshadeEnabled)
			L00480370(scene);
		return;
	}

	g_rendererType = RendererType_Main;
	g_current_renderer->MainSceneHook(scene);
	g_current_renderer->BuildGlowMarks(scene);
	g_current_renderer->RenderGlowMarks();
}

void D3dRendererShadowHook(SceneCompData* scene)
{
	g_rendererType = RendererType_Shadow;
	g_current_renderer->HangarShadowSceneHook(scene);
}

#ifdef DISABLED
//***************************************************************
// VertexMap implementation
//***************************************************************
VertexMap::~VertexMap()
{
	_map.clear();
}

// Returns false if Vertex isn't in the map. Otherwise returns
// true and Normal is also populated
bool VertexMap::find(const Vertex3 &Vertex, Vertex3 &Normal)
{
	Vertex3 V = Vertex;
	_adjustPrecision(V);
	std::string hash = hash_value(V);
	auto it = _map.find(hash);
	if (it == _map.end())
		return false;
	Normal = _map[hash];
	return true;
}

void VertexMap::_adjustPrecision(Vertex3 &Vertex)
{
	constexpr float precision = 1000.0f;
	float X = Vertex.x * precision;
	float Y = Vertex.y * precision;
	float Z = Vertex.z * precision;
	X = (float)floor(X);
	Y = (float)floor(Y);
	Z = (float)floor(Z);
	Vertex.x = X / precision;
	Vertex.y = Y / precision;
	Vertex.z = Z / precision;
}

void VertexMap::insert(const Vertex3 &Vertex, const Vertex3 &Normal)
{
	Vertex3 V, temp;
	V = Vertex;
	_adjustPrecision(V);
	if (find(V, temp))
		return;

	_map[hash_value(V)] = Normal;
}

void VertexMap::insert_or_add(const Vertex3 &Vertex, const Vertex3 &Normal)
{
	bool alreadyInMap;
	insert_or_add(Vertex, Normal, alreadyInMap);
}

void VertexMap::insert_or_add(const Vertex3 &Vertex, const Vertex3 &Normal, bool &alreadyInMap)
{
	Vertex3 V, temp;
	V = Vertex;
	_adjustPrecision(V);
	if (find(V, temp)) {
		temp.x += Normal.x;
		temp.y += Normal.y;
		temp.z += Normal.z;
		_map[hash_value(V)] = temp;
		alreadyInMap = true;
		return;
	}

	_map[hash_value(V)] = Normal;
	alreadyInMap = false;
}

void VertexMap::set(const Vertex3 &Vertex, const Vertex3 &Normal)
{
	Vertex3 V, temp;
	V = Vertex;
	_adjustPrecision(V);
	_map[hash_value(V)] = Normal;
}
#endif