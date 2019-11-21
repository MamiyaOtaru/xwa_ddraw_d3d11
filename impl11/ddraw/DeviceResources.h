// Copyright (c) 2014 J�r�my Ansel
// Licensed under the MIT license. See LICENSE.txt
// Extended for VR by Leo Reyes (c) 2019

#pragma once
#include "Matrices.h"
#include <vector>

// Also found in the Floating_GUI_RESNAME list:
extern const char *DC_TARGET_COMP_SRC_RESNAME;
extern const char *DC_LEFT_SENSOR_SRC_RESNAME;
extern const char *DC_LEFT_SENSOR_2_SRC_RESNAME;
extern const char *DC_RIGHT_SENSOR_SRC_RESNAME;
extern const char *DC_RIGHT_SENSOR_2_SRC_RESNAME;
extern const char *DC_SHIELDS_SRC_RESNAME;
extern const char *DC_SOLID_MSG_SRC_RESNAME;
extern const char *DC_BORDER_MSG_SRC_RESNAME;
extern const char *DC_LASER_BOX_SRC_RESNAME;
extern const char *DC_ION_BOX_SRC_RESNAME;
extern const char *DC_BEAM_BOX_SRC_RESNAME;
extern const char *DC_TOP_LEFT_SRC_RESNAME;
extern const char *DC_TOP_RIGHT_SRC_RESNAME;

typedef struct Box_struct {
	float x0, y0, x1, y1;
} Box;

typedef struct uvfloat4_struct {
	float x0, y0, x1, y1;
} uvfloat4;

const int LEFT_RADAR_HUD_BOX_IDX		= 0;
const int RIGHT_RADAR_HUD_BOX_IDX	= 1;
const int SHIELDS_HUD_BOX_IDX		= 2;
const int BEAM_HUD_BOX_IDX			= 3;
const int TARGET_HUD_BOX_IDX			= 4;
const int LEFT_MSG_HUD_BOX_IDX		= 5;
const int RIGHT_MSG_HUD_BOX_IDX		= 6;
const int TOP_LEFT_BOX_IDX			= 7;
const int TOP_RIGHT_BOX_IDX			= 8;
const int MAX_HUD_BOXES				= 9;
extern std::vector<const char *>g_HUDRegionNames;
// Convert a string into a *_HUD_BOX_IDX constant
int HUDRegionNameToIndex(char *name);

class DCHUDRegion {
public:
	Box coords;
	Box uv_erase_coords;
	uvfloat4 erase_coords;
	bool bLimitsComputed;
};

/*
 * This class stores the coordinates for each HUD Region : left radar, right radar, text
 * boxes, etc. It does not store the individual HUD elements within each HUD texture. For
 * that, look at DCElementSourceBox
 */
class DCHUDRegions {
public:
	std::vector<DCHUDRegion> boxes;

	DCHUDRegions();

	void Clear() {
		boxes.clear();
	}

	void ResetLimits() {
		for (unsigned int i = 0; i < boxes.size(); i++) 
			boxes[i].bLimitsComputed = false;
	}
};

const int LEFT_RADAR_DC_ELEM_SRC_IDX = 0;
const int RIGHT_RADAR_DC_ELEM_SRC_IDX = 1;
const int LASER_RECHARGE_DC_ELEM_SRC_IDX = 2;
const int SHIELD_RECHARGE_DC_ELEM_SRC_IDX = 3;
const int ENGINE_RECHARGE_DC_ELEM_SRC_IDX = 4;
const int BEAM_RECHARGE_DC_ELEM_SRC_IDX = 5;
const int SHIELDS_DC_ELEM_SRC_IDX = 6;
const int BEAM_DC_ELEM_SRC_IDX = 7;
const int TARGET_COMP_DC_ELEM_SRC_IDX = 8;
const int QUAD_LASERS_L_DC_ELEM_SRC_IDX = 9;
const int QUAD_LASERS_R_DC_ELEM_SRC_IDX = 10;
const int LEFT_MSG_DC_ELEM_SRC_IDX = 11;
const int RIGHT_MSG_DC_ELEM_SRC_IDX = 12;
const int SPEED_N_THROTTLE_DC_ELEM_SRC_IDX = 13;
const int MISSILES_DC_ELEM_SRC_IDX = 14;
const int NAME_TIME_DC_ELEM_SRC_IDX = 15;
const int NUM_CRAFTS_DC_ELEM_SRC_IDX = 16;
const int QUAD_LASERS_BOTH_DC_ELEM_SRC_IDX = 17;
const int DUAL_LASERS_L_DC_ELEM_SRC_IDX = 18;
const int DUAL_LASERS_R_DC_ELEM_SRC_IDX = 19;
const int DUAL_LASERS_BOTH_DC_ELEM_SRC_IDX = 20;
const int B_WING_LASERS_DC_ELEM_SRC_IDX = 21;
const int SIX_LASERS_BOTH_DC_ELEM_SRC_IDX = 22;
const int SIX_LASERS_L_DC_ELEM_SRC_IDX = 23;
const int SIX_LASERS_R_DC_ELEM_SRC_IDX = 24;
const int MAX_DC_SRC_ELEMENTS = 25;
extern std::vector<const char *>g_DCElemSrcNames;
// Convert a string into a *_DC_ELEM_SRC_IDX constant
int DCSrcElemNameToIndex(char *name);

class DCElemSrcBox {
public:
	Box uv_coords;
	Box coords;
	bool bComputed;

	DCElemSrcBox() {
		bComputed = false;
	}
};

/*
 * Stores the uv_coords and pixel coords for each individual HUD element. Examples of HUD elems
 * are:
 * Laser recharge rate, Shield recharage rate, Radars, etc.
 */
class DCElemSrcBoxes {
public:
	std::vector<DCElemSrcBox> src_boxes;

	void Clear() {
		src_boxes.clear();
	}

	DCElemSrcBoxes();
	void Reset() {
		for (unsigned int i = 0; i < src_boxes.size(); i++)
			src_boxes[i].bComputed = false;
	}
};

enum RenderMainColorKeyType
{
	RENDERMAIN_NO_COLORKEY,
	RENDERMAIN_COLORKEY_20,
	RENDERMAIN_COLORKEY_00,
};

class PrimarySurface;
class DepthSurface;
class BackbufferSurface;
class FrontbufferSurface;
class OffscreenSurface;

typedef struct HeadPosStruct {
	float x, y, z;
} HeadPos;

/* 2D Constant Buffers */
typedef struct MainShadersCBStruct {
	float scale, aspectRatio, parallax, brightness;
	float use_3D;
} MainShadersCBuffer;

typedef struct BarrelPixelShaderCBStruct {
	float k1, k2, k3;
	int unused;
} BarrelPixelShaderCBuffer;

#define BACKBUFFER_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM
//#define BLOOM_BUFFER_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM
#define BLOOM_BUFFER_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT
#define AO_DEPTH_BUFFER_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT
//#define AO_DEPTH_BUFFER_FORMAT DXGI_FORMAT_R32G32B32A32_FLOAT
//#define AO_MASK_FORMAT DXGI_FORMAT_R8_UINT
#define AO_MASK_FORMAT DXGI_FORMAT_B8G8R8A8_UNORM
#define HDR_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT

typedef struct BloomConfigStruct {
	float fSaturationStrength, fCockpitStrength, fEngineGlowStrength, fSparksStrength;
	float fLightMapsStrength, fLasersStrength, fHyperStreakStrength, fHyperTunnelStrength;
	float fTurboLasersStrength, fLensFlareStrength, fExplosionsStrength, fSunsStrength;
	float fCockpitSparksStrength, fMissileStrength;
	float uvStepSize1, uvStepSize2;
	int iNumPasses;
} BloomConfig;

typedef struct BloomPixelShaderCBStruct {
	float pixelSizeX, pixelSizeY, unused0, amplifyFactor;
	// 16 bytes
	float bloomStrength, uvStepSize, saturationStrength;
	int unused1;
	// 32 bytes
	int unused2;
	float unused3, depth_weight;
	int debug;
	// 48 bytes
} BloomPixelShaderCBuffer;

typedef struct SSAOPixelShaderCBStruct {
	float screenSizeX, screenSizeY, indirect_intensity, bias;
	// 16 bytes
	float intensity, near_sample_radius, black_level;
	int samples;
	// 32 bytes
	int z_division;
	float bentNormalInit, max_dist, power;
	// 48 bytes
	int debug;
	float moire_offset, amplifyFactor;
	int fn_enable;
	// 64 bytes
	float fn_max_xymult, fn_scale, fn_sharpness, nm_intensity_near;
	// 80 bytes
	float far_sample_radius, nm_intensity_far, ambient, amplifyFactor2;
	// 96 bytes
	float x0, y0, x1, y1; // Viewport limits in uv space
	// 112 bytes
	float invLightR, invLightG, invLightB, gamma;
	// 128 bytes
	float white_point, shadow_step_size, shadow_steps, aspect_ratio;
	// 144 bytes
	float vpScale[4];
	// 160 bytes
	int shadow_enable;
	float shadow_k, ssao_unused1, ssao_unused2;
	// 176 bytes
} SSAOPixelShaderCBuffer;

typedef struct ShadertoyCBStruct {
	float iMouse[3];
	float iTime;
	// 16 bytes
	float iResolution[2];
	float disk_size, unused2;
	// 32 bytes
	float x0, y0, x1, y1; // Limits in uv-coords of the viewport
	// 48 bytes
} ShadertoyCBuffer;

/* 3D Constant Buffers */
typedef struct VertexShaderCBStruct {
	float viewportScale[4];
	float aspect_ratio, cockpit_threshold, z_override, sz_override;
	float mult_z_override, bPreventTransform, bFullTransform;
} VertexShaderCBuffer;

typedef struct VertexShaderMatrixCBStruct {
	Matrix4 projEye;
	Matrix4 viewMat;
	Matrix4 fullViewMat;
} VertexShaderMatrixCB;

typedef struct float3_struct {
	float x, y, z;
} float3;

typedef struct float4_struct {
	float x, y, z, w;
} float4;

typedef struct PixelShaderMatrixCBStruct {
	//Matrix4 projEye;
	//Matrix4 viewMat;
	//Matrix4 fullViewMat;
	float4  LightVector;
	// 16 bytes
	float4  LightColor;
	// 32 bytes
	float4  LightVector2;
	// 48 bytes
	float4  LightColor2;
	// 64 bytes
} PixelShaderMatrixCB;

typedef struct PixelShaderCBStruct {
	float brightness;			// Used to control the brightness of some elements -- mostly for ReShade compatibility
	uint32_t DynCockpitSlots;
	uint32_t bUseCoverTexture;
	uint32_t bIsHyperspaceAnim;
	// 16 bytes
	
	uint32_t bIsLaser;
	uint32_t bIsLightTexture;
	uint32_t bIsEngineGlow;
	uint32_t bIsHyperspaceStreak;
	// 16 bytes

	float fBloomStrength;
	float fPosNormalAlpha;
	float fSSAOMaskVal;
	float fSSAOAlphaMult;
	// 16 bytes

	// 48 bytes total
} PixelShaderCBuffer;

// Pixel Shader constant buffer for the Dynamic Cockpit
const int MAX_DC_COORDS = 8;
typedef struct DCPixelShaderCBStruct {
	uvfloat4 src[MAX_DC_COORDS];
	// 4 * MAX_DC_COORDS * 4 = 128
	uvfloat4 dst[MAX_DC_COORDS];
	// 4 * MAX_DC_COORDS * 4 = 128
	uint32_t bgColor[MAX_DC_COORDS]; // 32-bit Background colors
	// 4 * MAX_DC_COORDS = 32

	float ct_brightness, unused1, unused2, unused3;
	// 304 bytes
} DCPixelShaderCBuffer;

typedef struct uv_coords_src_dst_struct {
	int src_slot[MAX_DC_COORDS];
	uvfloat4 dst[MAX_DC_COORDS];
	uint32_t uBGColor[MAX_DC_COORDS];
	int numCoords;
} uv_src_dst_coords;

typedef struct uv_coords_struct {
	uvfloat4 src[MAX_DC_COORDS];
	int numCoords;
} uv_coords;

const int MAX_TEXTURE_NAME = 128;
typedef struct dc_element_struct {
	uv_src_dst_coords coords;
	int erase_slots[MAX_DC_COORDS];
	int num_erase_slots;
	char name[MAX_TEXTURE_NAME];
	char coverTextureName[MAX_TEXTURE_NAME];
	//ComPtr<ID3D11ShaderResourceView> coverTexture = nullptr;
	//ID3D11ShaderResourceView *coverTexture = NULL;
	bool bActive, bNameHasBeenTested;
} dc_element;

typedef struct move_region_coords_struct {
	int region_slot[MAX_HUD_BOXES];
	uvfloat4 dst[MAX_HUD_BOXES];
	int numCoords;
} move_region_coords;

// SSAO Type
typedef enum {
	SSO_AMBIENT,
	SSO_DIRECTIONAL,
	SSO_BENT_NORMALS,
} SSAOTypeEnum;

class DeviceResources
{
public:
	DeviceResources();

	HRESULT Initialize();

	HRESULT OnSizeChanged(HWND hWnd, DWORD dwWidth, DWORD dwHeight);

	HRESULT LoadMainResources();

	HRESULT LoadResources();

	void InitInputLayout(ID3D11InputLayout* inputLayout);
	void InitVertexShader(ID3D11VertexShader* vertexShader);
	void InitPixelShader(ID3D11PixelShader* pixelShader);
	void InitTopology(D3D_PRIMITIVE_TOPOLOGY topology);
	void InitRasterizerState(ID3D11RasterizerState* state);
	HRESULT InitSamplerState(ID3D11SamplerState** sampler, D3D11_SAMPLER_DESC* desc);
	HRESULT InitBlendState(ID3D11BlendState* blend, D3D11_BLEND_DESC* desc);
	HRESULT InitDepthStencilState(ID3D11DepthStencilState* depthState, D3D11_DEPTH_STENCIL_DESC* desc);
	void InitVertexBuffer(ID3D11Buffer** buffer, UINT* stride, UINT* offset);
	void InitIndexBuffer(ID3D11Buffer* buffer);
	void InitViewport(D3D11_VIEWPORT* viewport);
	void InitVSConstantBuffer3D(ID3D11Buffer** buffer, const VertexShaderCBuffer* vsCBuffer);
	void InitVSConstantBufferMatrix(ID3D11Buffer** buffer, const VertexShaderMatrixCB* vsCBuffer);
	void InitPSConstantBufferMatrix(ID3D11Buffer** buffer, const PixelShaderMatrixCB* psCBuffer);
	void InitVSConstantBuffer2D(ID3D11Buffer** buffer, const float parallax, const float aspectRatio, const float scale, const float brightness, const float use_3D);
	void InitPSConstantBuffer2D(ID3D11Buffer** buffer, const float parallax, const float aspectRatio, const float scale, const float brightness);
	void InitPSConstantBufferBarrel(ID3D11Buffer** buffer, const float k1, const float k2, const float k3);
	void InitPSConstantBufferBloom(ID3D11Buffer ** buffer, const BloomPixelShaderCBuffer * psConstants);
	void InitPSConstantBufferSSAO(ID3D11Buffer ** buffer, const SSAOPixelShaderCBuffer * psConstants);
	void InitPSConstantBuffer3D(ID3D11Buffer** buffer, const PixelShaderCBuffer *psConstants);
	void InitPSConstantBufferDC(ID3D11Buffer ** buffer, const DCPixelShaderCBuffer * psConstants);
	void InitPSConstantBufferDeathStar(ID3D11Buffer ** buffer, const ShadertoyCBuffer * psConstants);

	void BuildHUDVertexBuffer(UINT width, UINT height);
	void BuildHyperspaceVertexBuffer(UINT width, UINT height);
	void ClearDynCockpitVector(dc_element DCElements[], int size);

	HRESULT RenderMain(char* buffer, DWORD width, DWORD height, DWORD bpp, RenderMainColorKeyType useColorKey = RENDERMAIN_COLORKEY_20);

	HRESULT RetrieveBackBuffer(char* buffer, DWORD width, DWORD height, DWORD bpp);

	UINT GetMaxAnisotropy();

	void CheckMultisamplingSupport();

	bool IsTextureFormatSupported(DXGI_FORMAT format);

	DWORD _displayWidth;
	DWORD _displayHeight;
	DWORD _displayBpp;
	DWORD _displayTempWidth;
	DWORD _displayTempHeight;
	DWORD _displayTempBpp;

	D3D_DRIVER_TYPE _d3dDriverType;
	D3D_FEATURE_LEVEL _d3dFeatureLevel;
	ComPtr<ID3D11Device> _d3dDevice;
	ComPtr<ID3D11DeviceContext> _d3dDeviceContext;
	ComPtr<IDXGISwapChain> _swapChain;
	ComPtr<ID3D11Texture2D> _backBuffer;
	ComPtr<ID3D11Texture2D> _offscreenBuffer;
	ComPtr<ID3D11Texture2D> _offscreenBufferR; // When SteamVR is used, _offscreenBuffer becomes the left eye and this one becomes the right eye
	ComPtr<ID3D11Texture2D> _offscreenBufferAsInput;
	ComPtr<ID3D11Texture2D> _offscreenBufferAsInputR; // When SteamVR is used, this is the right eye as input buffer
	// Dynamic Cockpit
	ComPtr<ID3D11Texture2D> _offscreenBufferDynCockpit;   // Used to render the targeting computer dynamically <-- Need to re-check this claim
	ComPtr<ID3D11Texture2D> _offscreenBufferDynCockpitBG; // Used to render the targeting computer dynamically <-- Need to re-check this claim
	ComPtr<ID3D11Texture2D> _offscreenAsInputDynCockpit;   // HUD elements buffer
	ComPtr<ID3D11Texture2D> _offscreenAsInputDynCockpitBG; // HUD element backgrounds buffer
	// Barrel effect
	ComPtr<ID3D11Texture2D> _offscreenBufferPost;  // This is the output of the barrel effect
	ComPtr<ID3D11Texture2D> _offscreenBufferPostR; // This is the output of the barrel effect for the right image when using SteamVR
	ComPtr<ID3D11Texture2D> _steamVRPresentBuffer; // This is the buffer that will be presented for SteamVR
	// ShaderToy effects
	ComPtr<ID3D11Texture2D> _shadertoyBuf;  // No MSAA
	ComPtr<ID3D11Texture2D> _shadertoyBufR; // No MSAA
	ComPtr<ID3D11Texture2D> _shadertoyAuxBuf;  // No MSAA
	ComPtr<ID3D11Texture2D> _shadertoyAuxBufR;  // No MSAA
	// Bloom
	ComPtr<ID3D11Texture2D> _offscreenBufferBloomMask;  // Used to render the bloom mask
	ComPtr<ID3D11Texture2D> _offscreenBufferBloomMaskR; // Used to render the bloom mask to the right image (SteamVR)
	ComPtr<ID3D11Texture2D> _offscreenBufferAsInputBloomMask;  // Used to resolve offscreenBufferBloomMask
	ComPtr<ID3D11Texture2D> _offscreenBufferAsInputBloomMaskR; // Used to resolve offscreenBufferBloomMaskR
	ComPtr<ID3D11Texture2D> _bloomOutput1; // Output from bloom pass 1
	ComPtr<ID3D11Texture2D> _bloomOutput2; // Output from bloom pass 2
	ComPtr<ID3D11Texture2D> _bloomOutputSum; // Bloom accummulator
	ComPtr<ID3D11Texture2D> _bloomOutput1R; // Output from bloom pass 1, right image (SteamVR)
	ComPtr<ID3D11Texture2D> _bloomOutput2R; // Output from bloom pass 2, right image (SteamVR)
	ComPtr<ID3D11Texture2D> _bloomOutputSumR; // Bloom accummulator (SteamVR)
	// Ambient Occlusion
	ComPtr<ID3D11Texture2D> _depthBuf;
	ComPtr<ID3D11Texture2D> _depthBufR;
	ComPtr<ID3D11Texture2D> _depthBufAsInput;
	ComPtr<ID3D11Texture2D> _depthBufAsInputR; // Used in SteamVR mode
	ComPtr<ID3D11Texture2D> _depthBuf2;
	ComPtr<ID3D11Texture2D> _depthBuf2R;
	ComPtr<ID3D11Texture2D> _depthBuf2AsInput;
	ComPtr<ID3D11Texture2D> _depthBuf2AsInputR; // Used in SteamVR mode
	ComPtr<ID3D11Texture2D> _normBuf;		// No MSAA so that it can be both bound to RTV and SRV
	ComPtr<ID3D11Texture2D> _normBufR;		// No MSAA
	ComPtr<ID3D11Texture2D> _bentBuf;		// No MSAA
	ComPtr<ID3D11Texture2D> _bentBufR;		// No MSAA
	ComPtr<ID3D11Texture2D> _ssaoBuf;		// No MSAA
	ComPtr<ID3D11Texture2D> _ssaoBufR;		// No MSAA
	ComPtr<ID3D11Texture2D> _ssaoMask;		// No MSAA
	ComPtr<ID3D11Texture2D> _ssaoMaskR;		// No MSAA

	// RTVs
	ComPtr<ID3D11RenderTargetView> _renderTargetView;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewR; // When SteamVR is used, _renderTargetView is the left eye, and this one is the right eye
	// Dynamic Cockpit
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDynCockpit; // Used to render the HUD to an offscreen buffer
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDynCockpitBG; // Used to render the HUD to an offscreen buffer
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDynCockpitAsInput; // RTV that writes to _offscreenBufferAsInputDynCockpit directly
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDynCockpitAsInputBG; // RTV that writes to _offscreenBufferAsInputDynCockpitBG directly
	// Barrel Effect
	ComPtr<ID3D11RenderTargetView> _renderTargetViewPost;  // Used for the barrel effect
	ComPtr<ID3D11RenderTargetView> _renderTargetViewPostR; // Used for the barrel effect (right image) when SteamVR is used.
	ComPtr<ID3D11RenderTargetView> _renderTargetViewSteamVRResize; // Used for the barrel effect
	// ShaderToy
	ComPtr<ID3D11RenderTargetView> _shadertoyRTV;
	ComPtr<ID3D11RenderTargetView> _shadertoyRTV_R;
	// Bloom
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloomMask  = NULL; // Renders to _offscreenBufferBloomMask
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloomMaskR = NULL; // Renders to _offscreenBufferBloomMaskR
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloom1; // Renders to bloomOutput1
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloom2; // Renders to bloomOutput2
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloomSum; // Renders to bloomOutputSum
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloom1R; // Renders to bloomOutput1R
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloom2R; // Renders to bloomOutput2R
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBloomSumR; // Renders to bloomOutputSumR
	// Ambient Occlusion
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDepthBuf;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDepthBufR;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDepthBuf2;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewDepthBuf2R;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewNormBuf;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewNormBufR;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBentBuf;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewBentBufR;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewSSAO;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewSSAO_R;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewSSAOMask;
	ComPtr<ID3D11RenderTargetView> _renderTargetViewSSAOMaskR;

	// SRVs
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputShaderResourceView;
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputShaderResourceViewR; // When SteamVR is enabled, this is the SRV for the right eye
	// Dynamic Cockpit
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputSRVDynCockpit;   // SRV for HUD elements without background
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputSRVDynCockpitBG; // SRV for HUD element backgrounds
	// Shadertoy
	ComPtr<ID3D11ShaderResourceView> _shadertoySRV;
	ComPtr<ID3D11ShaderResourceView> _shadertoySRV_R;
	ComPtr<ID3D11ShaderResourceView> _shadertoyAuxSRV;
	ComPtr<ID3D11ShaderResourceView> _shadertoyAuxSRV_R;
	// Bloom
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputBloomMaskSRV;
	ComPtr<ID3D11ShaderResourceView> _offscreenAsInputBloomMaskSRV_R;
	ComPtr<ID3D11ShaderResourceView> _bloomOutput1SRV; // SRV for bloomOutput1
	ComPtr<ID3D11ShaderResourceView> _bloomOutput2SRV; // SRV for bloomOutput2
	ComPtr<ID3D11ShaderResourceView> _bloomOutputSumSRV; // SRV for bloomOutputSum
	ComPtr<ID3D11ShaderResourceView> _bloomOutput1SRV_R; // SRV for bloomOutput1R
	ComPtr<ID3D11ShaderResourceView> _bloomOutput2SRV_R; // SRV for bloomOutput2R
	ComPtr<ID3D11ShaderResourceView> _bloomOutputSumSRV_R; // SRV for bloomOutputSumR
	// Ambient Occlusion
	ComPtr<ID3D11ShaderResourceView> _depthBufSRV;   // SRV for depthBufAsInput
	ComPtr<ID3D11ShaderResourceView> _depthBufSRV_R; // SRV for depthBufAsInputR
	ComPtr<ID3D11ShaderResourceView> _depthBuf2SRV;   // SRV for depthBuf2AsInput
	ComPtr<ID3D11ShaderResourceView> _depthBuf2SRV_R; // SRV for depthBuf2AsInputR
	ComPtr<ID3D11ShaderResourceView> _normBufSRV;    // SRV for normBuf
	ComPtr<ID3D11ShaderResourceView> _normBufSRV_R;  // SRV for normBufR
	ComPtr<ID3D11ShaderResourceView> _bentBufSRV;    // SRV for bentBuf
	ComPtr<ID3D11ShaderResourceView> _bentBufSRV_R;  // SRV for bentBufR
	ComPtr<ID3D11ShaderResourceView> _ssaoBufSRV; // SRV for ssaoBuf
	ComPtr<ID3D11ShaderResourceView> _ssaoBufSRV_R; // SRV for ssaoBuf
	ComPtr<ID3D11ShaderResourceView> _ssaoMaskSRV; // SRV for ssaoMask
	ComPtr<ID3D11ShaderResourceView> _ssaoMaskSRV_R; // SRV for ssaoMaskR

	ComPtr<ID3D11Texture2D> _depthStencilL;
	ComPtr<ID3D11Texture2D> _depthStencilR;
	ComPtr<ID3D11DepthStencilView> _depthStencilViewL;
	ComPtr<ID3D11DepthStencilView> _depthStencilViewR;

	ComPtr<ID3D11VertexShader> _mainVertexShader;
	ComPtr<ID3D11InputLayout> _mainInputLayout;
	ComPtr<ID3D11PixelShader> _mainPixelShader;
	ComPtr<ID3D11PixelShader> _mainPixelShaderBpp2ColorKey20;
	ComPtr<ID3D11PixelShader> _mainPixelShaderBpp2ColorKey00;
	ComPtr<ID3D11PixelShader> _mainPixelShaderBpp4ColorKey20;
	ComPtr<ID3D11PixelShader> _basicPixelShader;
	ComPtr<ID3D11PixelShader> _barrelPixelShader;
	ComPtr<ID3D11PixelShader> _bloomHGaussPS;
	ComPtr<ID3D11PixelShader> _bloomVGaussPS;
	ComPtr<ID3D11PixelShader> _bloomCombinePS;
	ComPtr<ID3D11PixelShader> _bloomBufferAddPS;
	ComPtr<ID3D11PixelShader> _computeNormalsPS;
	ComPtr<ID3D11PixelShader> _ssaoPS;
	ComPtr<ID3D11PixelShader> _ssaoBlurPS;
	ComPtr<ID3D11PixelShader> _ssaoAddPS;
	ComPtr<ID3D11PixelShader> _ssdoDirectPS;
	ComPtr<ID3D11PixelShader> _ssdoDirectBentNormalsPS;
	ComPtr<ID3D11PixelShader> _ssdoDirectHDRPS;
	ComPtr<ID3D11PixelShader> _ssdoIndirectPS;
	ComPtr<ID3D11PixelShader> _ssdoAddPS;
	ComPtr<ID3D11PixelShader> _ssdoAddHDRPS;
	ComPtr<ID3D11PixelShader> _ssdoAddBentNormalsPS;
	ComPtr<ID3D11PixelShader> _ssdoBlurPS;
	ComPtr<ID3D11PixelShader> _deathStarPS;
	ComPtr<ID3D11PixelShader> _hyperspacePS;
	ComPtr<ID3D11PixelShader> _singleBarrelPixelShader;
	ComPtr<ID3D11RasterizerState> _mainRasterizerState;
	ComPtr<ID3D11SamplerState> _mainSamplerState;
	ComPtr<ID3D11BlendState> _mainBlendState;
	ComPtr<ID3D11DepthStencilState> _mainDepthState;
	ComPtr<ID3D11Buffer> _mainVertexBuffer;
	ComPtr<ID3D11Buffer> _steamVRPresentVertexBuffer; // Used in SteamVR mode to correct the image presented on the monitor
	ComPtr<ID3D11Buffer> _mainIndexBuffer;
	ComPtr<ID3D11Texture2D> _mainDisplayTexture;
	ComPtr<ID3D11ShaderResourceView> _mainDisplayTextureView;
	ComPtr<ID3D11Texture2D> _mainDisplayTextureTemp;
	ComPtr<ID3D11ShaderResourceView> _mainDisplayTextureViewTemp;

	ComPtr<ID3D11VertexShader> _vertexShader;
	ComPtr<ID3D11VertexShader> _sbsVertexShader;
	ComPtr<ID3D11VertexShader> _passthroughVertexShader;
	ComPtr<ID3D11InputLayout> _inputLayout;
	ComPtr<ID3D11PixelShader> _pixelShaderTexture;
	ComPtr<ID3D11PixelShader> _pixelShaderDC;
	ComPtr<ID3D11PixelShader> _pixelShaderEmptyDC;
	ComPtr<ID3D11PixelShader> _pixelShaderHUD;
	ComPtr<ID3D11PixelShader> _pixelShaderSolid;
	ComPtr<ID3D11PixelShader> _pixelShaderClearBox;
	ComPtr<ID3D11RasterizerState> _rasterizerState;
	ComPtr<ID3D11Buffer> _VSConstantBuffer;
	ComPtr<ID3D11Buffer> _VSMatrixBuffer;
	ComPtr<ID3D11Buffer> _PSMatrixBuffer;
	ComPtr<ID3D11Buffer> _PSConstantBuffer;
	ComPtr<ID3D11Buffer> _PSConstantBufferDC;
	ComPtr<ID3D11Buffer> _barrelConstantBuffer;
	ComPtr<ID3D11Buffer> _bloomConstantBuffer;
	ComPtr<ID3D11Buffer> _ssaoConstantBuffer;
	ComPtr<ID3D11Buffer> _hyperspaceStarConstantBuffer;
	ComPtr<ID3D11Buffer> _mainShadersConstantBuffer;
	
	ComPtr<ID3D11Buffer> _barrelEffectVertBuffer;
	ComPtr<ID3D11Buffer> _HUDVertexBuffer;
	ComPtr<ID3D11Buffer> _clearHUDVertexBuffer;
	ComPtr<ID3D11Buffer> _hyperspaceVertexBuffer;
	bool _bHUDVerticesReady;

	// Dynamic Cockpit coverTextures:
	ComPtr<ID3D11ShaderResourceView> dc_coverTexture[MAX_DC_SRC_ELEMENTS];

	BOOL _useAnisotropy;
	BOOL _useMultisampling;
	DXGI_SAMPLE_DESC _sampleDesc;
	UINT _backbufferWidth;
	UINT _backbufferHeight;
	DXGI_RATIONAL _refreshRate;
	bool _are16BppTexturesSupported;
	bool _use16BppMainDisplayTexture;
	DWORD _mainDisplayTextureBpp;

	float clearColor[4];
	float clearDepth;
	bool sceneRendered;
	bool sceneRenderedEmpty;
	bool inScene;
	bool inSceneBackbufferLocked;

	PrimarySurface* _primarySurface;
	DepthSurface* _depthSurface;
	BackbufferSurface* _backbufferSurface;
	FrontbufferSurface* _frontbufferSurface;
	OffscreenSurface* _offscreenSurface;
};
