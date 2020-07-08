#pragma once

#include <stdio.h>
#include <vector>
#include "Vectors.h"
#include "Matrices.h"
#include "config.h"
#include <headers/openvr.h>


extern vr::IVRSystem* g_pHMD;
extern vr::IVRCompositor* g_pVRCompositor;
extern vr::IVRScreenshots* g_pVRScreenshots;
extern vr::TrackedDevicePose_t g_rTrackedDevicePose;
extern uint32_t g_steamVRWidth, g_steamVRHeight; // The resolution recommended by SteamVR is stored here
extern bool g_bSteamVREnabled; // The user sets this flag to true to request support for SteamVR.
extern bool g_bSteamVRInitialized; // The system will set this flag after SteamVR has been initialized
extern bool g_bUseSteamVR; // The system will set this flag if the user requested SteamVR and SteamVR was initialized properly
extern bool g_bEnableSteamVR_QPC;
extern const bool DEFAULT_INTERLEAVED_REPROJECTION;
extern const bool DEFAULT_STEAMVR_POS_FROM_FREEPIE;
extern bool g_bInterleavedReprojection;
extern bool g_bResetHeadCenter; // Reset the head center on startup
extern bool g_bSteamVRDistortionEnabled;
extern vr::HmdMatrix34_t g_EyeMatrixLeft, g_EyeMatrixRight;
extern vr::HmdMatrix34_t g_EyeMatrixLeft, g_EyeMatrixRight;
extern Matrix4 g_EyeMatrixLeftInv, g_EyeMatrixRightInv;
extern vr::HmdMatrix34_t g_EyeMatrixLeft, g_EyeMatrixRight;
extern Matrix4 g_projLeft, g_projRight;
extern Matrix4 g_FullProjMatrixLeft, g_FullProjMatrixRight, g_viewMatrix;
//float g_fMetricMult = DEFAULT_METRIC_MULT, 
extern float g_fFrameTimeRemaining;
extern int g_iSteamVR_Remaining_ms, g_iSteamVR_VSync_ms;
extern Vector3 g_headCenter;
extern bool g_bSteamVRPosFromFreePIE;
extern float g_fVR_FOV;

/*
 *	SteamVR specific functions declarations
 */

bool InitSteamVR();
void ShutDownSteamVR();
bool UpdateXWAHackerFOV();
void CycleFOVSetting();
float ComputeRealVertFOV();
float ComputeRealHorzFOV();
float RealVertFOVToRawFocalLength(float real_FOV);

void projectSteamVR(float X, float Y, float Z, vr::EVREye eye, float& x, float& y, float& z);
void ProcessSteamVREyeMatrices(vr::EVREye eye);
char* GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError = NULL);
bool WaitGetPoses();

void DumpMatrix34(FILE* file, const vr::HmdMatrix34_t& m);
void DumpMatrix44(FILE* file, const vr::HmdMatrix44_t& m);
void DumpMatrix4(FILE* file, const Matrix4& mat);
void ShowMatrix4(const Matrix4& mat, char* name);
void ShowHmdMatrix34(const vr::HmdMatrix34_t& mat, char* name);
void ShowHmdMatrix44(const vr::HmdMatrix44_t& mat, char* name);
void Matrix4toHmdMatrix44(const Matrix4 & m4, vr::HmdMatrix44_t & mat);
Matrix4 HmdMatrix44toMatrix4(const vr::HmdMatrix44_t& mat);
Matrix4 HmdMatrix34toMatrix4(const vr::HmdMatrix34_t& mat);
void ShowVector4(const Vector4& X, char* name);