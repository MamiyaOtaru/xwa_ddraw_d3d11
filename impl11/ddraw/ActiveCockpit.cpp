#include "common.h"
#include "ActiveCockpit.h"
#include "DynamicCockpit.h"
#include "utils.h"
#include "Direct3DTexture.h"

#include <mmsystem.h>

/*********************************************************/
// ACTIVE COCKPIT
Vector4 g_contOriginWorldSpace = Vector4(-0.15f, -0.05f, 0.3f, 1.0f); // This is the origin of the controller in 3D, in world-space coords
Vector4 g_controllerForwardVector = Vector4(0.0f, 0.0f, 1.0f, 0.0f); // Forward direction in the controller's frame of reference
Vector4 g_controllerUpVector = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
// This is the direction in which the controller is pointing in world-space coords.
// Comes from transforming g_controllerForwardVector into viewspace coords.
Vector4 g_contDirWorldSpace;
Vector3 g_LaserPointer3DIntersection = Vector3(0.0f, 0.0f, 10000.0f);
float g_fBestIntersectionDistance = FLT_MAX, g_fLaserPointerLength = 0.0f;
int g_iBestIntersTexIdx = -1; // The index into g_ACElements where the intersection occurred
bool g_bActiveCockpitEnabled = false, g_bACActionTriggered = false, g_bACLastTriggerState = false, g_bACTriggerState = false;
bool g_bFreePIEControllerButtonDataAvailable = false;
ac_element g_ACElements[MAX_AC_TEXTURES_PER_COCKPIT] = { 0 };
int g_iNumACElements = 0, g_iLaserDirSelector = 3;
// DEBUG vars
Vector3 g_debug_v0, g_debug_v1, g_debug_v2;
bool g_bDumpLaserPointerDebugInfo = false;
Vector3 g_LPdebugPoint;
float g_fLPdebugPointOffset = 0.0f;
// DEBUG vars

ACJoyEmulSettings g_ACJoyEmul;        // Stores data needed to configure VR controllers (handedness, motion range, deadzones)
ACJoyMapping      g_ACJoyMappings[2]; // Maps VR buttons to AC actions
ACPointerData     g_ACPointerData;    // Tells us which controller and button is used to activate AC controls

bool IsContinousAction(WORD* action)
{
	return (action != nullptr &&
			action[0] == 0xFF &&
			// The first joystick button is fire, so it's a continuous action.
			// The second joystick button is target/roll. In order to enable roll, it must be a continous action.
			(action[1] == AC_JOYBUTTON1_FAKE_VK_CODE || action[1] == AC_JOYBUTTON2_FAKE_VK_CODE));
}

/*
 * Executes the action defined by "action" as per the Active Cockpit
 * definitions.
 */
void ACRunAction(WORD* action, struct joyinfoex_tag* pji) {
	// Scan codes from: http://www.philipstorr.id.au/pcbook/book3/scancode.htm
	// Scan codes: https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
	// Based on code from: https://stackoverflow.com/questions/18647053/sendinput-not-equal-to-pressing-key-manually-on-keyboard-in-c
	// Virtual key codes: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
	// How to send extended scan codes
	// https://stackoverflow.com/questions/36972524/winapi-extended-keyboard-scan-codes/36976260#36976260
	// https://stackoverflow.com/questions/26283738/how-to-use-extended-scancodes-in-sendinput
	INPUT input[MAX_AC_ACTION_LEN];
	bool bEscapedAction = (action[0] == 0xe0);

	if (action[0] == 0) { // void action, skip
		//log_debug("[DBG] [AC] Skipping VOID action");
		return;
	}

	// Special internal action: these actions don't need to synthesize any input
	if (action[0] == 0xFF)
	{
		switch (action[1])
		{
		case AC_HOLOGRAM_FAKE_VK_CODE:
			g_bDCHologramsVisible = !g_bDCHologramsVisible;
			return;
		case AC_JOYBUTTON1_FAKE_VK_CODE:
			if (pji != nullptr) {
				pji->dwButtons |= 1;
				pji->dwButtonNumber = max(pji->dwButtonNumber, 1);
			}
			return;
		case AC_JOYBUTTON2_FAKE_VK_CODE:
			if (pji != nullptr) {
				pji->dwButtons |= 2;
				pji->dwButtonNumber = max(pji->dwButtonNumber, 2);
			}
			return;
		case AC_JOYBUTTON3_FAKE_VK_CODE:
			if (pji != nullptr) {
				pji->dwButtons |= 4;
				pji->dwButtonNumber = max(pji->dwButtonNumber, 3);
			}
			return;
		case AC_JOYBUTTON4_FAKE_VK_CODE:
			if (pji != nullptr) {
				pji->dwButtons |= 8;
				pji->dwButtonNumber = max(pji->dwButtonNumber, 4);
			}
			return;
		case AC_JOYBUTTON5_FAKE_VK_CODE:
			if (pji != nullptr) {
				pji->dwButtons |= 16;
				pji->dwButtonNumber = max(pji->dwButtonNumber, 5);
			}
			return;
		}
		return;
	}

	//if (bEscapedAction)
	//	log_debug("[DBG] [AC] Executing escaped code");
	//log_debug("[DBG] [AC] Running action: ");
	//DisplayACAction(action);

	// Copy & initialize the scan codes
	int i = 0, j = bEscapedAction ? 1 : 0;
	while (action[j] && j < MAX_AC_ACTION_LEN) {
		input[i].ki.wScan = action[j];
		input[i].type = INPUT_KEYBOARD;
		input[i].ki.time = 0;
		input[i].ki.wVk = 0;
		input[i].ki.dwExtraInfo = 0;
		input[i].ki.dwFlags = KEYEVENTF_SCANCODE;
		if (bEscapedAction) {
			input[i].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			//input[i].ki.dwExtraInfo = GetMessageExtraInfo();
		}
		i++; j++;
	}

	j = bEscapedAction ? 1 : 0;
	while (action[j] && j < MAX_AC_ACTION_LEN) {
		input[i].ki.wScan = action[j];
		input[i].type = INPUT_KEYBOARD;
		input[i].ki.time = 0;
		input[i].ki.wVk = 0;
		input[i].ki.dwExtraInfo = 0;
		input[i].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
		if (bEscapedAction) {
			input[i].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			//input[i].ki.dwExtraInfo = GetMessageExtraInfo();
		}
		i++; j++;
	}

	// Send keydown/keyup events in one go: (this is the only way I found to enable the arrow/escaped keys)
	SendInput(i, input, sizeof(INPUT));
}

/*
 * Converts a string representation of a hotkey to a series of scan codes
 */
void TranslateACAction(WORD* scanCodes, char* action, bool *bIsACActivator) {
	// XWA keyboard reference:
	// http://isometricland.net/keyboard/keyboard-diagram-star-wars-x-wing-alliance.php?sty=15&lay=1&fmt=0&ten=1
	// Scan code tables:
	// http://www.philipstorr.id.au/pcbook/book3/scancode.htm
	// https://www.shsu.edu/~csc_tjm/fall2000/cs272/scan_codes.html
	// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
	int len = strlen(action);
	const char* ptr = action, * cursor;
	int i, j;

	if (bIsACActivator != nullptr)
		*bIsACActivator = false;

	// Translate to uppercase
	for (i = 0; i < len; i++)
		action[i] = toupper(action[i]);
	//log_debug("[DBG] [AC] Translating [%s]", action);

	// Stop further parsing if this is a void action
	if (strstr(action, "VOID") != NULL)
	{
		scanCodes[0] = 0;
		return;
	}

	j = 0;
	/*
	// Function keys must be handled separately because SHIFT+Fn have unique
	// scan codes
	if (strstr("F1", action) != NULL) {
		if      (strstr(action, "SHIFT") != NULL)	scanCodes[j] = 0x54;
		else if (strstr(action, "CTRL") != NULL)	scanCodes[j] = 0x5E;
		else if (strstr(action, "ALT") != NULL)		scanCodes[j] = 0x68;
		else										scanCodes[j] = 0x3B;
		return;
	}
	// End of function keys
	*/

	// Composite keys
	ptr = action;
	if ((cursor = strstr(action, "SHIFT")) != NULL) { scanCodes[j++] = 0x2A; ptr = cursor + strlen("SHIFT "); }
	if ((cursor = strstr(action, "CTRL"))  != NULL) { scanCodes[j++] = 0x1D; ptr = cursor + strlen("CTRL "); }
	if ((cursor = strstr(action, "ALT"))   != NULL) { scanCodes[j++] = 0x38; ptr = cursor + strlen("ALT "); }

	// Process the function keys
	if (strstr(ptr, "F") != NULL) {
		char next = *(ptr + 1);
		if (isdigit(next)) {
			// This is a function key, convert all the digits after the F
			int numkey = atoi(ptr + 1);
			scanCodes[j++] = 0x3B + numkey - 1;
			scanCodes[j] = 0; // Terminate the scan code list
			return;
		}
	}

	// Process the arrow keys
	if (strstr(ptr, "ARROW") != NULL)
	{
		scanCodes[j++] = 0xE0;
		if (strstr(ptr, "LEFT")  != NULL) scanCodes[j++] = 0x4B;
		if (strstr(ptr, "RIGHT") != NULL) scanCodes[j++] = 0x4D;
		if (strstr(ptr, "UP")    != NULL) scanCodes[j++] = 0x48;
		if (strstr(ptr, "DOWN")  != NULL) scanCodes[j++] = 0x50;

		//if (strstr(ptr, "LEFT") != NULL)		scanCodes[j++] = MapVirtualKey(VK_LEFT, MAPVK_VK_TO_VSC);	// CONFIRMED: 0x4B
		//if (strstr(ptr, "RIGHT") != NULL)	scanCodes[j++] = MapVirtualKey(VK_RIGHT, MAPVK_VK_TO_VSC);	// CONFIRMED: 0x4D
		//if (strstr(ptr, "UP") != NULL)  		scanCodes[j++] = MapVirtualKey(VK_UP, MAPVK_VK_TO_VSC);		// CONFIRMED: 0x48
		//if (strstr(ptr, "DOWN") != NULL) 	scanCodes[j++] = MapVirtualKey(VK_DOWN, MAPVK_VK_TO_VSC);	// CONFIRMED: 0x50
		scanCodes[j] = 0;
		return;
	}

	// Process other special keys
	{
		if (strstr(ptr, "TAB") != NULL) // CONFIRMED: 0x0F
		{
			scanCodes[j++] = MapVirtualKey(VK_TAB, MAPVK_VK_TO_VSC);
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "ENTER") != NULL)
		{
			scanCodes[j++] = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
			scanCodes[j] = 0;
			return;
		}

		if (isdigit(*ptr)) { // CONFIRMED
			int digit = *ptr - '0';
			if (digit == 0) digit += 10;
			scanCodes[j++] = 0x02 - 1 + digit;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, ";") != NULL) {
			scanCodes[j++] = 0x27;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "'") != NULL) {
			scanCodes[j++] = 0x28;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "COMMA") != NULL) {
			scanCodes[j++] = 0x33;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "PERIOD") != NULL) {
			scanCodes[j++] = 0x34;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "BACKSPACE") != NULL) {
			scanCodes[j++] = MapVirtualKey(VK_BACK, MAPVK_VK_TO_VSC);
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "SPACE") != NULL || strstr(ptr, "ACTIVATE") != NULL) {
			scanCodes[j++] = 0x39;
			scanCodes[j] = 0;
			if (bIsACActivator != nullptr)
				*bIsACActivator = true;
			return;
		}

		if (strstr(ptr, "HOLOGRAM") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_HOLOGRAM_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "JOYBUTTON1") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_JOYBUTTON1_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "JOYBUTTON2") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_JOYBUTTON2_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "JOYBUTTON3") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_JOYBUTTON3_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "JOYBUTTON4") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_JOYBUTTON4_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "JOYBUTTON5") != NULL) {
			scanCodes[0] = 0xFF;
			scanCodes[1] = AC_JOYBUTTON5_FAKE_VK_CODE;
			return;
		}

		if (strstr(ptr, "[") != NULL) {
			scanCodes[j++] = 0x1A;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "]") != NULL) {
			scanCodes[j++] = 0x1B;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "\\") != NULL) {
			scanCodes[j++] = 0x2B;
			scanCodes[j] = 0;
			return;
		}

		if (strstr(ptr, "ESC") != NULL) {
			scanCodes[j++] = MapVirtualKey(VK_ESCAPE, MAPVK_VK_TO_VSC);
			scanCodes[j] = 0;
			return;
		}
	}

	if (strlen(ptr) > 1)
	{
		log_debug("[DBG] [AC] WARNING: Parsing a probably unknown action: [%s]", ptr);
	}

	// Regular single-char keys
	scanCodes[j++] = (WORD)MapVirtualKey(*ptr, MAPVK_VK_TO_VSC);
	if (j < MAX_AC_ACTION_LEN)
		scanCodes[j] = 0; // Terminate the scan code list
}

void DisplayACAction(WORD* scanCodes) {
	std::stringstream ss;
	int i = 0;
	while (scanCodes[i] && i < MAX_AC_ACTION_LEN) {
		ss << std::hex << scanCodes[i] << ", ";
		i++;
	}
	std::string s = ss.str();
	log_debug("[DBG] [AC] %s", s.c_str());
}

/*
 * Loads an "action" row:
 * "action" = ACTION, x0,y0, x1,y1
 */
bool LoadACAction(char* buf, float width, float height, ac_uv_coords* coords)
{
	float x0, y0, x1, y1;
	int res = 0, idx = coords->numCoords;
	char* substr = NULL;
	char action[50];

	if (idx >= MAX_AC_COORDS_PER_TEXTURE) {
		log_debug("[DBG] [AC] Too many actions already loaded for this texture");
		return false;
	}

	substr = strchr(buf, '=');
	if (substr == NULL) {
		log_debug("[DBG] [AC] Missing '=' in '%s', skipping", buf);
		return false;
	}
	// Skip the equal sign:
	substr++;
	// Skip white space chars:
	while (*substr == ' ' || *substr == '\t')
		substr++;

	try {
		int len;

		//src_slot = -1;
		action[0] = 0;
		len = ReadNameFromLine(substr, action);
		if (len == 0)
			return false;

		// Parse the rest of the parameters
		substr += len + 1;
		res = sscanf_s(substr, "%f, %f, %f, %f", &x0, &y0, &x1, &y1);
		if (res < 4) {
			log_debug("[DBG] [DC] ERROR (skipping), expected at least 4 elements in '%s'", substr);
		}
		else {
			bool dummy;
			strcpy_s(&(coords->action_name[idx][0]), 16, action);
			TranslateACAction(&(coords->action[idx][0]), action, &dummy);
			//DisplayACAction(&(coords->action[idx][0]));

			x0 /= width;
			y0 /= height;
			x1 /= width;
			y1 /= height;
			// Images are stored upside down in an OPT (I think), so we need to flip the Y axis:
			y0 = 1.0f - y0;
			y1 = 1.0f - y1;
			if (y0 > y1) std::swap(y0, y1);
			coords->area[idx].x0 = x0;
			coords->area[idx].y0 = y0;
			coords->area[idx].x1 = x1;
			coords->area[idx].y1 = y1;
			// Flip the coordinates if necessary
			/*if (x0 != -1.0f && x1 != -1.0f) {
				if (x0 > x1)
				{
					// Swap coords in the X-axis:
					//float x = coords->area[idx].x0;
					//coords->area[idx].x0 = coords->area[idx].x1;
					//coords->area[idx].x1 = x;
					// Mirror the X-axis:
					coords->area[idx].x0 = 1.0f - coords->area[idx].x0;
					coords->area[idx].x1 = 1.0f - coords->area[idx].x1;
				}
			}
			if (y0 != -1.0f && y1 != -1.0f) {
				if (y0 > y1)
				{
					// Swap coords in the Y-axis:
					//float y = coords->area[idx].y0;
					//coords->area[idx].y0 = coords->area[idx].y1;
					//coords->area[idx].y1 = y;

					// Mirror the Y-axis:
					coords->area[idx].y0 = 1.0f - coords->area[idx].y0;
					coords->area[idx].y1 = 1.0f - coords->area[idx].y1;
				}
			}*/
			coords->numCoords++;
		}
	}
	catch (...) {
		log_debug("[DBG] [AC] Could not read uv coords from: %s", buf);
		return false;
	}
	return true;
}

/*
 * Load the AC params for an individual cockpit.
 * Resets g_DCElements (if we're not rendering in 3D), and the move regions.
 */
bool LoadIndividualACParams(char* sFileName) {
	log_debug("[DBG] [AC] Loading Active Cockpit params for [%s]...", sFileName);
	FILE* file;
	int error = 0, line = 0;
	static int lastACElemSelected = -1;
	float tex_width = 1, tex_height = 1;

	// Reset the Active Cockpit elements? We probably don't need this, because the code modifies existing
	// AC slots
	//g_iNumACElements = 0;

	try {
		error = fopen_s(&file, sFileName, "rt");
	}
	catch (...) {
		log_debug("[DBG] Could not load [%s]", sFileName);
	}

	if (error != 0) {
		log_debug("[DBG] Error %d when loading [%s]", error, sFileName);
		return false;
	}

	char buf[256], param[128], svalue[128];
	int param_read_count = 0;
	float value = 0.0f;

	while (fgets(buf, 256, file) != NULL) {
		line++;
		// Skip comments and blank lines
		if (buf[0] == ';' || buf[0] == '#')
			continue;
		if (strlen(buf) == 0)
			continue;

		if (sscanf_s(buf, "%s = %s", param, 128, svalue, 128) > 0) {
			value = (float)atof(svalue);

			if (buf[0] == '[') {
				// This is a new AC element.
				ac_element ac_elem;
				strcpy_s(ac_elem.name, MAX_TEXTURE_NAME, buf + 1);
				// Get rid of the trailing ']'
				char* end = strstr(ac_elem.name, "]");
				if (end != NULL)
					*end = 0;
				// See if we have this AC element already
				lastACElemSelected = isInVector(ac_elem.name, g_ACElements, g_iNumACElements);
				if (lastACElemSelected > -1) {
					g_ACElements[lastACElemSelected].coords.numCoords = 0;
					log_debug("[DBG] [AC] Resetting coords of existing AC elem @ idx: %d", lastACElemSelected);
				}
				else if (g_iNumACElements < MAX_AC_TEXTURES_PER_COCKPIT) {
					//log_debug("[DBG] [AC] New ac_elem.name: [%s], id: %d",
					//	ac_elem.name, g_iNumACElements);
					//ac_elem.idx = g_iNumACElements; // Generate a unique ID
					ac_elem.coords = { 0 };
					ac_elem.bActive = false;
					ac_elem.bNameHasBeenTested = false;
					g_ACElements[g_iNumACElements] = ac_elem;
					lastACElemSelected = g_iNumACElements;
					g_iNumACElements++;
					//log_debug("[DBG] [AC] Added new ac_elem, count: %d", g_iNumACElements);
				}
				else {
					if (g_iNumACElements >= MAX_AC_TEXTURES_PER_COCKPIT)
						log_debug("[DBG] [AC] ERROR: Max g_iNumACElements: %d reached", g_iNumACElements);
				}
			}
			else if (_stricmp(param, "texture_size") == 0) {
				// We can re-use LoadDCCoverTextureSize here, it's the same format (but different tag)
				LoadDCCoverTextureSize(buf, &tex_width, &tex_height);
				//log_debug("[DBG] [AC] texture size: %0.3f, %0.3f", tex_width, tex_height);
			}
			else if (_stricmp(param, "action") == 0) {
				if (g_iNumACElements == 0) {
					log_debug("[DBG] [AC] ERROR. Line %d, g_ACElements is empty, cannot add action", line);
					continue;
				}
				if (lastACElemSelected == -1) {
					log_debug("[DBG] [AC] ERROR. Line %d, 'action' tag without a corresponding texture section.", line);
					continue;
				}
				//log_debug("[DBG] [AC] Loading action for: [%s]", g_ACElements[lastACElemSelected].name);
				LoadACAction(buf, tex_width, tex_height, &(g_ACElements[lastACElemSelected].coords));
				// DEBUG
				//g_ACElements[lastACElemSelected].width = (short)tex_width;
				//g_ACElements[lastACElemSelected].height = (short)tex_height;
				/*ac_uv_coords *coords = &(g_ACElements[lastACElemSelected].coords);
				int idx = coords->numCoords - 1;
				log_debug("[DBG] [AC] Action (v is flipped): (%0.3f, %0.3f)-(%0.3f, %0.3f)",
					coords->area[idx].x0, coords->area[idx].y0,
					coords->area[idx].x1, coords->area[idx].y1);*/
				// DEBUG
			}

		}
	}
	fclose(file);
	return true;
}

int isInVector(char* name, ac_element* ac_elements, int num_elems) {
	for (int i = 0; i < num_elems; i++) {
		if (stristr(name, ac_elements[i].name) != NULL)
			return i;
	}
	return -1;
}