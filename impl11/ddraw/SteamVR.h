#pragma once

#include <stdio.h>
#include <vector>
#include "Vectors.h"
#include "Matrices.h"
#include "config.h"
#include <openvr.h>

constexpr float DEFAULT_STEAMVR_OVERLAY_WIDTH = 5.0f;

extern vr::IVRSystem* g_pHMD;
extern vr::IVRChaperone* g_pChaperone;
extern vr::IVRCompositor* g_pVRCompositor;
extern vr::IVROverlay* g_pVROverlay;
extern vr::VROverlayHandle_t g_VR2Doverlay; // Handle to the specific overlay displaying the 2D rendered scenes.
extern vr::IVRScreenshots* g_pVRScreenshots;
extern vr::TrackedDevicePose_t g_rTrackedDevicePose;
extern uint32_t g_steamVRWidth, g_steamVRHeight; // The resolution recommended by SteamVR is stored here
extern bool g_bSteamVREnabled; // The user sets this flag to true to request support for SteamVR.
extern bool g_bSteamVRInitialized; // The system will set this flag after SteamVR has been initialized
extern bool g_bUseSteamVR; // The system will set this flag if the user requested SteamVR and SteamVR was initialized properly
extern bool g_bSteamVRMirrorWindowLeftEye;
extern const bool DEFAULT_INTERLEAVED_REPROJECTION;
extern const bool DEFAULT_STEAMVR_POS_FROM_FREEPIE;
extern bool g_bInterleavedReprojection;
extern bool g_bSteamVRDistortionEnabled;
extern vr::HmdMatrix34_t g_EyeMatrixLeft, g_EyeMatrixRight;
//float g_fMetricMult = DEFAULT_METRIC_MULT, 
extern float g_fFrameTimeRemaining;
extern int g_iSteamVR_Remaining_ms, g_iSteamVR_VSync_ms;
extern bool g_bSteamVRPosFromFreePIE;
extern float g_fOBJ_Z_MetricMult, g_fOBJGlobalMetricMult, g_fOBJCurMetricScale;
extern void* g_pSurface;
extern bool g_bTogglePostPresentHandoff;
extern bool g_bSteamVRMirrorWindowLeftEye;

namespace VRButtons
{
	enum VRButtons
	{
		TRIGGER = 0,
		GRIP,
		BUTTON_1,
		BUTTON_2,

		PAD_LEFT,
		PAD_RIGHT,
		PAD_UP,
		PAD_DOWN,
		PAD_CLICK,

		MAX // Sentinel, do not remove
	};
};

struct ControllerState
{
	Matrix4  pose;
	bool     bIsValid;
	bool     buttons[VRButtons::MAX];
	float    trackPadX, trackPadY;
	float    yaw, pitch, roll; // As reported by SteamVR
	float    centerYaw, centerPitch, centerRoll;
	uint32_t packetNum; // Internal, do not modify

	ControllerState()
	{
		pose.identity();
		for (int i = 0; i < VRButtons::MAX; i++)
			buttons[i] = false;
		bIsValid  = false;
		trackPadX = 0;
		trackPadY = 0;
		yaw = pitch = roll = 0;
		centerYaw = centerPitch = centerRoll = 0;
		packetNum = 0xFFFFFFFF;
	}
};
extern ControllerState g_prevContStates[2];
extern ControllerState g_contStates[2];

struct VRKeybState
{
	bool    bVisible;
	bool    bRendered;
	Matrix4 Transform;

	VRKeybState()
	{
		bVisible  = false;
		bRendered = false;
		Transform.identity();
	}
};

extern VRKeybState g_vrKeybState;

/*
 *	SteamVR specific functions declarations
 */

bool InitSteamVR();
void ShutDownSteamVR();

void projectSteamVR(float X, float Y, float Z, vr::EVREye eye, float& x, float& y, float& z);
void ProcessSteamVREyeMatrices(vr::EVREye eye);
char* GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError = NULL);
void GetSteamVRPositionalData(float* yaw, float* pitch, float* roll, float* x, float* y, float* z, Matrix4* m4_hmdPose);
// WaitGetPoses is now called from the CockpitLook hook.
//bool WaitGetPoses();

void quatToEuler(vr::HmdQuaternionf_t q, float* yaw, float* roll, float* pitch);
vr::HmdQuaternionf_t eulerToQuat(float yaw, float pitch, float roll);
vr::HmdMatrix33_t quatToMatrix(vr::HmdQuaternionf_t q);
vr::HmdQuaternionf_t rotationToQuaternion(vr::HmdMatrix34_t m);
Matrix3 HmdMatrix34toMatrix3(const vr::HmdMatrix34_t& mat);
Matrix3 HmdMatrix33toMatrix3(const vr::HmdMatrix33_t& mat);

void DumpMatrix34(FILE* file, const vr::HmdMatrix34_t& m);
void DumpMatrix44(FILE* file, const vr::HmdMatrix44_t& m);
void DumpMatrix4(FILE* file, const Matrix4& mat);
void ShowMatrix4(const Matrix4& mat, char* name);
void ShowHmdMatrix34(const vr::HmdMatrix34_t& mat, char* name);
void ShowHmdMatrix44(const vr::HmdMatrix44_t& mat, char* name);
void Matrix4toHmdMatrix44(const Matrix4 & m4, vr::HmdMatrix44_t & mat);
void Matrix4toHmdMatrix34(const Matrix4& m4, vr::HmdMatrix34_t& mat);
Matrix4 HmdMatrix44toMatrix4(const vr::HmdMatrix44_t& mat);
Matrix4 HmdMatrix34toMatrix4(const vr::HmdMatrix34_t& mat);
void ShowVector4(const Vector4& X, char* name);