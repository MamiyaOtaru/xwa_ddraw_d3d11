// Copyright (c) 2016-2018 Reimar D�ffinger
// Licensed under the MIT license. See LICENSE.txt

#include "common.h"
#include "config.h"

#include <mmsystem.h>
#include <xinput.h>

#include "XWAObject.h"
#include "SharedMem.h"
#include "joystick.h"
#include "HiResTimer.h"
#include "SteamVR.h"
#include "ActiveCockpit.h"

extern PlayerDataEntry *PlayerDataTable;
extern uint32_t *g_playerIndex;

// Gimbal Lock Fix
bool g_bEnableGimbalLockFix = false, g_bGimbalLockFixActive = false;
bool g_bEnableRudder = true, g_bYTSeriesShip = false;

// How much roll is applied when the ship is doing yaw. Set this to 0
// to have a completely flat yaw, as in the YT-series ships.
float g_fRollFromYawScale = -0.8f;

// Acceleration and deceleration rates in degrees per second (this is the 2nd derivative)
float g_fYawAccelRate_s   = 5.0f;
float g_fPitchAccelRate_s = 5.0f;
float g_fRollAccelRate_s  = 5.0f;

// The maximum turn rate (in degrees per second) for each axis (1st derivative)
float g_fMaxYawRate_s   = 70.0f;
float g_fMaxPitchRate_s = 70.0f;
float g_fMaxRollRate_s  = 100.0f;

float g_fTurnRateScaleThr_0   = 0.3f;
float g_fTurnRateScaleThr_100 = 0.6f;
float g_fMaxTurnAccelRate_s   = 3.0f;
bool g_bThrottleModulationEnabled = true;

float g_fMouseRangeX = 256.0f, g_fMouseRangeY = 256.0f;

float g_fMouseDeltaX = 0.0f, g_fMouseDeltaY = 0.0f;
float g_fMouseDecelRate_s = 700.0f;

constexpr int MAX_RAW_INPUT_ENTRIES = 128;
RAWINPUT rawInputBuffer[MAX_RAW_INPUT_ENTRIES];

extern bool g_bRendering3D;

#pragma comment(lib, "winmm")
#pragma comment(lib, "XInput9_1_0")

#undef min
#undef max
#include <algorithm>

inline float sign(float val)
{
	return (val >= 0.0f) ? 1.0f : -1.0f;
}

inline float clamp(float val, float min, float max)
{
	if (val < min) val = min;
	if (val > max) val = max;
	return val;
}

inline float lerp(float x, float y, float s) {
	return x + s * (y - x);
}

float SignedReduceClamp(float val, float delta, float abs_min, float abs_max)
{
	float s = sign(val);
	val = fabs(val);
	val -= delta;
	if (val < abs_min) val = abs_min;
	if (val > abs_max) val = abs_max;
	return s * val;
}

void SendMouseEvent(float dx, float dy, bool bLeft, bool bRight)
{
	constexpr int MAX_ACTION_LEN = 12;
	INPUT input[MAX_ACTION_LEN];

	int i = 0;
	input[i].ki.wScan = 0;
	input[i].type = INPUT_MOUSE;
	input[i].mi.dx = (LONG)dx;
	input[i].mi.dy = (LONG)dy;
	input[i].mi.dwFlags = MOUSEEVENTF_MOVE | (bLeft ? MOUSEEVENTF_LEFTDOWN : 0) | (bLeft ? MOUSEEVENTF_RIGHTDOWN : 0);
	input[i].mi.mouseData = 0;
	input[i].mi.dwExtraInfo = NULL;
	input[i].mi.time = 0;
	i++;

	input[i].ki.wScan = 0;
	input[i].type = INPUT_MOUSE;
	input[i].mi.dx = 0;
	input[i].mi.dy = 0;
	input[i].mi.dwFlags = (bLeft ? MOUSEEVENTF_LEFTUP : 0) | (bLeft ? MOUSEEVENTF_RIGHTUP : 0);
	input[i].mi.mouseData = 0;
	input[i].mi.dwExtraInfo = NULL;
	input[i].mi.time = 0;
	i++;

	SendInput(i, input, sizeof(INPUT));
}

void EmulMouseWithVRControllers()
{
	const int ptrIdx = g_ACPointerData.contIdx;
	static bool bFirstTime = true;
	static WORD escScanCodes[2];
	if (bFirstTime)
	{
		char action[] = "ESC";
		TranslateACAction(escScanCodes, action, nullptr);
		bFirstTime = false;
	}

	const float dx =  g_contStates[ptrIdx].trackPadX * g_ACPointerData.mouseSpeedX;
	const float dy = -g_contStates[ptrIdx].trackPadY * g_ACPointerData.mouseSpeedY;
	const bool  bLeftClick = (!g_prevContStates[ptrIdx].buttons[VRButtons::TRIGGER] && g_contStates[ptrIdx].buttons[VRButtons::TRIGGER]);

	SendMouseEvent(dx, dy, bLeftClick, false);

	// Send the ESC command
	if (!g_prevContStates[ptrIdx].buttons[VRButtons::PAD_CLICK] && g_contStates[ptrIdx].buttons[VRButtons::PAD_CLICK])
		ACRunAction(escScanCodes, nullptr);

	// Actions have been processed, we can update the previous state now
	for (int i = 0; i < 2; i++)
		g_prevContStates[i] = g_contStates[i];
}

// timeGetTime emulation.
// if it is called in a tight loop it will call Sleep()
// prevents high CPU usage due to busy loop
DWORD emulGetTime()
{
	static DWORD oldtime;
	static DWORD count;
	DWORD time = timeGetTime();
	if (time != oldtime)
	{
		oldtime = time;
		count = 0;
	}
	// Trigger value and sleep value derived by trial and error.
	// Trigger values down to 10 with sleep value 8 do not seem to affect performance
	// even at 60 FPS, and trigger values up to 1000 with sleep value 1 still seem effective
	// to reduce CPU load on fast modern computers.
	if (++count >= 20)
	{
		Sleep(2);
		time = timeGetTime();
		count = 0;
	}
	return time;
}

static int needsJoyEmul()
{
	JOYCAPS caps = {};
	if (joyGetDevCaps(0, &caps, sizeof(caps)) != JOYERR_NOERROR ||
	    !(caps.wCaps & JOYCAPS_HASZ) || caps.wNumAxes <= 2 ||
	    // Assume for now that wMid == 0x45e means "Gamepad"
	    // Steam controller: pid == 0x28e
	    // Xbox controller: pid == 0x2ff
	    caps.wMid == 0x45e)
	{
		// Probably the joystick is just an emulation from a gamepad.
		// Rather try to use it as gamepad directly then.
		XINPUT_STATE state;
		if (XInputGetState(0, &state) == ERROR_SUCCESS) return 2;
	}
	UINT cnt = joyGetNumDevs();
	for (unsigned i = 0; i < cnt; ++i)
	{
		JOYINFOEX jie;
		memset(&jie, 0, sizeof(jie));
		jie.dwSize = sizeof(jie);
		jie.dwFlags = JOY_RETURNALL;
		UINT res = joyGetPosEx(0, &jie);
		if (res == JOYERR_NOERROR)
			return 0;
	}
	return 1;
}

UINT WINAPI emulJoyGetNumDevs(void)
{
	if (g_config.JoystickEmul < 0) {
		g_config.JoystickEmul = needsJoyEmul();
	}
	if (!g_config.JoystickEmul) {
		return joyGetNumDevs();
	}
	return 1;
}

static UINT joyYmax, joyZmax, joyZmin;

UINT WINAPI emulJoyGetDevCaps(UINT_PTR joy, struct tagJOYCAPSA *pjc, UINT size)
{
	if (!g_config.JoystickEmul) {
		UINT res = joyGetDevCaps(joy, pjc, size);
		if (g_config.InvertYAxis && joy == 0 && pjc && size == 0x194) joyYmax = pjc->wYmax;
		if (joy == 0 && pjc && size == 0x194) {
			joyZmax = pjc->wZmax;
			joyZmin = pjc->wZmin;
			if (joyZmin > joyZmax) {
				UINT temp = joyZmin;
				joyZmin = joyZmax;
				joyZmax = temp;
			}
		}
		return res;
	}
	if (joy != 0) return MMSYSERR_NODRIVER;
	if (size != 0x194) return MMSYSERR_INVALPARAM;
	memset(pjc, 0, size);
	if (g_config.JoystickEmul == 2) {
		pjc->wXmax = 65536;
		pjc->wYmax = 65535;
		pjc->wZmax = 255;
		pjc->wRmax = 65536;
		pjc->wUmax = 65536;
		pjc->wVmax = 255;
		pjc->wNumButtons = 14;
		pjc->wMaxButtons = 14;
		pjc->wNumAxes = 6;
		pjc->wMaxAxes = 6;
		pjc->wCaps = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASV | JOYCAPS_HASPOV | JOYCAPS_POV4DIR;
		return JOYERR_NOERROR;
	}
	pjc->wXmax = 512;
	pjc->wYmax = 512;
	pjc->wZmax = 512;
	pjc->wRmax = 512;
	pjc->wNumButtons = 5;
	pjc->wMaxButtons = 5;
	// wNumAxes should probably stay at 2 here because needsJoyEmul() compares against
	// 2 num axes to decide whether or not to use XInput.
	pjc->wNumAxes = 6;
	pjc->wMaxAxes = 6;
	pjc->wCaps = JOYCAPS_HASZ | JOYCAPS_HASR;
	return JOYERR_NOERROR;
}

static DWORD lastGetPos;

static const DWORD povmap[16] = {
	JOY_POVCENTERED, // nothing
	JOY_POVFORWARD, // DPAD_UP
	JOY_POVBACKWARD, // DPAD_DOWN
	JOY_POVCENTERED, // up and down
	JOY_POVLEFT, // DPAD_LEFT
	(270 + 45) * 100, // left and up
	(180 + 45) * 100, // down and left
	JOY_POVLEFT, // left and up and down
	JOY_POVRIGHT, // DPAD_RIGHT
	45 * 100, // up and right
	(90 + 45) * 100, // right and down
	JOY_POVRIGHT, // right and up and down

	// As from start, but  with both right and left pressed
	JOY_POVCENTERED, // nothing
	JOY_POVFORWARD, // DPAD_UP
	JOY_POVBACKWARD, // DPAD_DOWN
	JOY_POVCENTERED, // up and down
};

// See:
// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputbuffer?redirectedfrom=MSDN
// https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input
// https://stackoverflow.com/questions/28879021/winapi-getrawinputbuffer
// https://stackoverflow.com/questions/59809804/rawinput-winapi-getrawinputbuffer-and-message-handling
// Returns 0 if no events were read
// DISABLED: I suspect that registering for low-level events messes the main message pump
#ifdef DISABLED
int ReadRawMouseData(int *iDeltaX, int *iDeltaY)
{
	uint32_t rawInputBufferSize = sizeof(RAWINPUT) * MAX_RAW_INPUT_ENTRIES;

	*iDeltaX = 0; *iDeltaY = 0;
	//bool nonzero = false;
	// MAGIC NUMBER WARNING:
	// For some reason, the actual data is shifted by 8 bytes.
	// This 8 is probably the diff between sizeof(RAWINPUT) (40) and header.dwSize (48)
	RAWINPUT* raw = (RAWINPUT*)((uint32_t)rawInputBuffer + 8);

	int res = GetRawInputBuffer(rawInputBuffer, &rawInputBufferSize, sizeof(RAWINPUTHEADER));
	int count = res;
	/*if (count) {
		log_debug("[DBG] count: %d, rawInputBufferSize: %d", count, rawInputBufferSize);
		nonzero = true;
	}*/

	while (count > 0) {
		// The trackpad becomes RIM_TYPEMOUSE, but additional mice have a different type
		// (probably RIM_TYPEHID). Since we're only registering mice, we can ignore the
		// type and move on...
		//if (raw->header.dwType == RIM_TYPEMOUSE)
		{
			/*log_debug("[DBG] RMD.MOUSE event, %d, %d; %d, %d, %d",
				raw->data.mouse.lLastX, raw->data.mouse.lLastY,
				raw->data.mouse.ulButtons, raw->data.mouse.usButtonFlags,
				raw->data.mouse.ulExtraInformation);*/
			*iDeltaX += raw->data.mouse.lLastX;
			*iDeltaY += raw->data.mouse.lLastY;
		}

		raw = NEXTRAWINPUTBLOCK(raw);
		count--;
	}

	/*if (nonzero) {
		log_debug("[DBG] Delta: %d, %d", *iDeltaX, *iDeltaY);
		log_debug("[DBG] ---------------------------------");
	}*/
	return res;
}

void ResetRawMouseInput()
{
	int deltaX, deltaY;
	ReadRawMouseData(&deltaX, &deltaY);
	g_fMouseDeltaX = g_fMouseDeltaY = 0.0f;
}
#endif

UINT WINAPI emulJoyGetPosEx(UINT joy, struct joyinfoex_tag *pji)
{
	// Tell the joystick hook when to disable the joystick
	if (g_pSharedDataJoystick != NULL) {
		g_pSharedDataJoystick->GimbalLockFixActive = g_bGimbalLockFixActive;
		g_pSharedDataJoystick->JoystickEmulationEnabled = (g_config.JoystickEmul != 0);
	}

	if (!g_config.JoystickEmul) {
		UINT res = joyGetPosEx(joy, pji);
		if (g_config.InvertYAxis && joyYmax > 0) pji->dwYpos = joyYmax - pji->dwYpos;

		// Only swap the X-Z joystick axes if the flag is set and we're not in the gunner turret
		if (!PlayerDataTable[*g_playerIndex].gunnerTurretActive && g_config.SwapJoystickXZAxes) {
			DWORD X = pji->dwXpos;
			pji->dwXpos = pji->dwRpos;
			pji->dwRpos = X;
		}

		if (g_config.InvertThrottle) pji->dwZpos = (joyZmax - pji->dwZpos) + joyZmin;
		// dwXpos: x-axis goes from 0 (left) to 32767 (center) to 65535 (right)
		// dwYpos: y-axis goes from 0 (up)   to 32767 (center) to 65535 (down)
		// dwRpos: z-axis goes from 0 (left) to 32767 (center) to 65535 (right)
		if (g_bGimbalLockFixActive && g_pSharedDataJoystick != NULL)
		{
			// Only run this section if the joystick hook isn't present
			if (!(g_pSharedDataJoystick->JoystickHookPresent)) {
				float normYaw   = 2.0f * (pji->dwXpos / 65535.0f - 0.5f);
				float normPitch = 2.0f * (pji->dwYpos / 65535.0f - 0.5f);
				float normRoll  = 2.0f * (pji->dwRpos / 65535.0f - 0.5f);
				g_pSharedDataJoystick->JoystickYaw   = normYaw;
				g_pSharedDataJoystick->JoystickPitch = normPitch;
				g_pSharedDataJoystick->JoystickRoll  = normRoll;

				// Nullify the joystick input
				pji->dwXpos = 32767;
				pji->dwYpos = 32767;
				pji->dwRpos = 32767;
			}
		}
		return res;
	}

	if (joy != 0) return MMSYSERR_NODRIVER;
	if (pji->dwSize != 0x34) return MMSYSERR_INVALPARAM;

	// XInput
	if (g_config.JoystickEmul == 2) {
		XINPUT_STATE state;
		XInputGetState(0, &state);
		pji->dwFlags = JOY_RETURNALL;
		pji->dwXpos = state.Gamepad.sThumbLX + 32768;
		pji->dwYpos = state.Gamepad.sThumbLY + 32768;
		if (!g_config.InvertYAxis) pji->dwYpos = 65536 - pji->dwYpos;
		// The 65536 value breaks XWA with in-game invert Y axis option
		pji->dwYpos = std::min(pji->dwYpos, DWORD(65535));
		if (g_config.XInputTriggerAsThrottle)
		{
			pji->dwZpos = g_config.XInputTriggerAsThrottle & 1 ? state.Gamepad.bLeftTrigger : state.Gamepad.bRightTrigger;
		}
		pji->dwRpos = state.Gamepad.sThumbRX + 32768;
		pji->dwUpos = state.Gamepad.sThumbRY + 32768;
		pji->dwVpos = state.Gamepad.bLeftTrigger;
		pji->dwButtons = 0;
		// Order matches XBox One controller joystick emulation default
		// A, B, X, Y first
		pji->dwButtons |= (state.Gamepad.wButtons & 0xf000) >> 12;
		// Shoulder buttons next
		pji->dwButtons |= (state.Gamepad.wButtons & 0x300) >> 4;
		// start and back, for some reason in flipped order compared to XINPUT
		pji->dwButtons |= (state.Gamepad.wButtons & 0x10) << 3;
		pji->dwButtons |= (state.Gamepad.wButtons & 0x20) << 1;
		// Thumb buttons
		pji->dwButtons |= (state.Gamepad.wButtons & 0xc0) << 2;
		// Triggers last, they are not mapped in the joystick emulation
		if (g_config.XInputTriggerAsThrottle != 1 &&
			state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) pji->dwButtons |= 0x400;
		if (g_config.XInputTriggerAsThrottle != 2 &&
			state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) pji->dwButtons |= 0x800;
		// These are not defined by XINPUT, and can't be remapped by the
		// XWA user interface as they will be buttons above 12, but map them just in case
		pji->dwButtons |= (state.Gamepad.wButtons & 0xc00) << 2;

		// XWA can map only 12 buttons, so map dpad to POV
		pji->dwPOV = povmap[state.Gamepad.wButtons & 15];

		pji->dwButtonNumber = 0;
		for (int i = 0; i < 32; i++)
		{
			if ((pji->dwButtons >> i) & 1) ++pji->dwButtonNumber;
		}
		return JOYERR_NOERROR;
	}

	DWORD now = GetTickCount();
	// Assume we started a new game
	if ((now - lastGetPos) > 5000)
	{
		SetCursorPos(240, 240);
		GetAsyncKeyState(VK_LBUTTON);
		GetAsyncKeyState(VK_RBUTTON);
		GetAsyncKeyState(VK_MBUTTON);
		GetAsyncKeyState(VK_XBUTTON1);
		GetAsyncKeyState(VK_XBUTTON2);
	}
	lastGetPos = now;
	POINT pos;
	GetCursorPos(&pos);

	pji->dwXpos = 256; // This is the center position for this axis
	pji->dwYpos = 256;
	pji->dwZpos = 256; // Throttle
	pji->dwRpos = 256; // Rudder (roll)

	// Mouse input
	{
		//if (!g_bGimbalLockFixActive)
		{
			// This is the original code by Reimar
			pji->dwXpos = static_cast<DWORD>(std::min(256.0f + (pos.x - 240.0f) * g_config.MouseSensitivity, 512.0f));
			pji->dwYpos = static_cast<DWORD>(std::min(256.0f + (pos.y - 240.0f) * g_config.MouseSensitivity, 512.0f));
		}
		// DISABLED: I suspect that registering for low-level events messes the main message pump
#ifdef DISABLED
		else // if (g_bRendering3D)
		{
			static float lastTime = g_HiResTimer.global_time_s;
			const float now = g_HiResTimer.global_time_s;
			const float elapsedTime = now - lastTime;

			int iDeltaX = 0, iDeltaY = 0;
			if (ReadRawMouseData(&iDeltaX, &iDeltaY))
			{
				g_fMouseDeltaX = clamp(g_fMouseDeltaX + (float)iDeltaX * g_config.MouseSensitivity,
					-g_fMouseRangeX, g_fMouseRangeX);
				g_fMouseDeltaY = clamp(g_fMouseDeltaY + (float)iDeltaY * g_config.MouseSensitivity,
					-g_fMouseRangeY, g_fMouseRangeY);
			}
			else
			{
				//g_fMouseDeltaX *= 0.5f;
				//g_fMouseDeltaY *= 0.5f;

				g_fMouseDeltaX = SignedReduceClamp(g_fMouseDeltaX, elapsedTime * g_fMouseDecelRate_s, 0.0f, g_fMouseRangeX);
				g_fMouseDeltaY = SignedReduceClamp(g_fMouseDeltaY, elapsedTime * g_fMouseDecelRate_s, 0.0f, g_fMouseRangeY);
			}
			const float deltaX = clamp(g_fMouseDeltaX / g_fMouseRangeX, -1.0f, 1.0f);
			const float deltaY = clamp(g_fMouseDeltaY / g_fMouseRangeY, -1.0f, 1.0f);

			pji->dwXpos = static_cast<DWORD>((deltaX + 1.0f) / 2.0f * 512.0f);
			pji->dwYpos = static_cast<DWORD>((deltaY + 1.0f) / 2.0f * 512.0f);

			lastTime = now;
		}
#endif

		pji->dwButtons = 0;
		pji->dwButtonNumber = 0;
		if (GetAsyncKeyState(VK_LBUTTON)) {
			pji->dwButtons |= 1;
			++pji->dwButtonNumber;
		}
		if (GetAsyncKeyState(VK_RBUTTON)) {
			pji->dwButtons |= 2;
			++pji->dwButtonNumber;
		}
		if (GetAsyncKeyState(VK_MBUTTON)) {
			pji->dwButtons |= 4;
			++pji->dwButtonNumber;
		}
		if (GetAsyncKeyState(VK_XBUTTON1)) {
			pji->dwButtons |= 8;
			++pji->dwButtonNumber;
		}
		if (GetAsyncKeyState(VK_XBUTTON2)) {
			pji->dwButtons |= 16;
			++pji->dwButtonNumber;
		}
	}

	float normYaw = 0, normPitch = 0;
	if (g_bUseSteamVR && g_bActiveCockpitEnabled)
	{
		// Support VR controllers (quick-and-dirty approach, the proper way is probably to add code to the
		// joystick hook so that bindings can be configured).

		const int joyIdx = g_ACJoyEmul.joyHandIdx;
		const int thrIdx = g_ACJoyEmul.thrHandIdx;

		if (g_ACJoyEmul.joystickEnabled)
		{
			static Vector4 rightAnchor;
			if (!(g_prevContStates[joyIdx].buttons[VRButtons::GRIP]) && g_contStates[joyIdx].buttons[VRButtons::GRIP])
			{
				rightAnchor = g_contStates[joyIdx].pose * Vector4(0, 0, 0, 1);;
			}

			if (g_contStates[joyIdx].buttons[VRButtons::GRIP])
			{
				Vector4 current = g_contStates[joyIdx].pose * Vector4(0, 0, 0, 1);
				float yaw   = clamp(current.x - rightAnchor.x, -g_ACJoyEmul.joyHalfRangeX, g_ACJoyEmul.joyHalfRangeX) / g_ACJoyEmul.joyHalfRangeX;
				float pitch = clamp(current.z - rightAnchor.z, -g_ACJoyEmul.joyHalfRangeZ, g_ACJoyEmul.joyHalfRangeZ) / g_ACJoyEmul.joyHalfRangeZ;

				// Apply deadzone
				const float s_yaw   = sign(yaw);
				const float s_pitch = sign(pitch);

				yaw   = fabs(yaw);
				pitch = fabs(pitch);

				if (yaw < g_ACJoyEmul.deadZonePerc)
					yaw = 0.0f;
				else
					yaw = lerp(0.0f, 1.0f, clamp(yaw / (1.0f - g_ACJoyEmul.deadZonePerc), 0.0f, 1.0f));

				if (pitch < g_ACJoyEmul.deadZonePerc)
					pitch = 0.0f;
				else
					pitch = lerp(0.0f, 1.0f, clamp(pitch / (1.0f - g_ACJoyEmul.deadZonePerc), 0.0f, 1.0f));

				yaw   *= s_yaw;
				pitch *= s_pitch;

				normYaw     = yaw;
				normPitch   = pitch;
				pji->dwXpos = (DWORD)(512.0f * ((normYaw   / 2.0f) + 0.5f));
				pji->dwYpos = (DWORD)(512.0f * ((normPitch / 2.0f) + 0.5f));
			}
		}

		if (g_ACJoyEmul.throttleEnabled)
		{
			static Vector4 anchor;
			static float normThrottle   = 0.0f;
			static float anchorThrottle = 0.0f;
			if (!(g_prevContStates[thrIdx].buttons[VRButtons::GRIP]) && g_contStates[thrIdx].buttons[VRButtons::GRIP])
			{
				anchor = g_contStates[thrIdx].pose * Vector4(0, 0, 0, 1);
				anchorThrottle = normThrottle;
			}

			if (g_contStates[thrIdx].buttons[VRButtons::GRIP])
			{
				Vector4 current = g_contStates[thrIdx].pose * Vector4(0, 0, 0, 1);
				const float D   = (current.z - anchor.z) / g_ACJoyEmul.thrHalfRange;
				normThrottle    = clamp(anchorThrottle + D, -1.0f, 1.0f);
			}

			pji->dwZpos = (DWORD)(512.0f * ((normThrottle / 2.0f) + 0.5f));
		}

		// Synthesize mouse motion
		if (!g_bRendering3D)
		{
			EmulMouseWithVRControllers();
		}
		else
		{
			// Synthesize joystick button clicks and run AC actions associated with VR buttons
			pji->dwButtons = 0;
			pji->dwButtonNumber = 0;
			for (int contIdx = 0; contIdx < 2; contIdx++)
			{
				for (int buttonIdx = 0; buttonIdx < VRButtons::MAX; buttonIdx++)
				{
					if (IsContinousAction(g_ACJoyMappings[contIdx].action[buttonIdx]))
					{
						if (g_contStates[contIdx].buttons[buttonIdx])
							ACRunAction(g_ACJoyMappings[contIdx].action[buttonIdx], pji);
					}
					else
					{
						if (!g_prevContStates[contIdx].buttons[buttonIdx] && g_contStates[contIdx].buttons[buttonIdx])
							ACRunAction(g_ACJoyMappings[contIdx].action[buttonIdx], pji);
					}
				}
				// Actions have been processed, we can update the previous state now
				g_prevContStates[contIdx] = g_contStates[contIdx];
			}
		}
	}
	else
	{
		bool bCtrlKey = (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0x8000;
		if (bCtrlKey)
		{
			if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
				pji->dwRpos = static_cast<DWORD>(std::max(256 - 256 * g_config.KbdSensitivity, 0.0f));
			}
			if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
				pji->dwRpos = static_cast<DWORD>(std::min(256 + 256 * g_config.KbdSensitivity, 512.0f));
			}
		}
		else
		{
			if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
				pji->dwXpos = static_cast<DWORD>(std::max(256 - 256 * g_config.KbdSensitivity, 0.0f));
			}
			if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
				pji->dwXpos = static_cast<DWORD>(std::min(256 + 256 * g_config.KbdSensitivity, 512.0f));
			}
		}

		if (GetAsyncKeyState(VK_UP) & 0x8000) {
			pji->dwYpos = static_cast<DWORD>(std::max(256 - 256 * g_config.KbdSensitivity, 0.0f));
		}
		if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
			pji->dwYpos = static_cast<DWORD>(std::min(256 + 256 * g_config.KbdSensitivity, 512.0f));
		}
		if (g_config.InvertYAxis) pji->dwYpos = 512 - pji->dwYpos;
		// Normalize each axis to the range -1..1:
		normYaw   = 2.0f * (pji->dwXpos / 512.0f - 0.5f);
		normPitch = 2.0f * (pji->dwYpos / 512.0f - 0.5f);
	}

	if (g_pSharedDataJoystick != NULL) {
		g_pSharedDataJoystick->JoystickYaw   = normYaw;
		g_pSharedDataJoystick->JoystickPitch = normPitch;
		g_pSharedDataJoystick->JoystickRoll  = 0.0f;
	}

	if (g_bGimbalLockFixActive)
	{
		// Nullify the joystick input, but only if we're inside the cockpit
		// (that's what g_bGimbalLockActive does)
		pji->dwXpos = 256;
		pji->dwYpos = 256;
		pji->dwZpos = 256;
		pji->dwRpos = 256;
	}

	return JOYERR_NOERROR;
}
