#pragma once

// DYNAMIC COCKPIT

// Also found in the Floating_GUI_RESNAME list:
extern const char* DC_TARGET_COMP_SRC_RESNAME;
extern const char* DC_LEFT_SENSOR_SRC_RESNAME;
extern const char* DC_LEFT_SENSOR_2_SRC_RESNAME;
extern const char* DC_RIGHT_SENSOR_SRC_RESNAME;
extern const char* DC_RIGHT_SENSOR_2_SRC_RESNAME;
extern const char* DC_SHIELDS_SRC_RESNAME;
extern const char* DC_SOLID_MSG_SRC_RESNAME;
extern const char* DC_BORDER_MSG_SRC_RESNAME;
extern const char* DC_LASER_BOX_SRC_RESNAME;
extern const char* DC_ION_BOX_SRC_RESNAME;
extern const char* DC_BEAM_BOX_SRC_RESNAME;
extern const char* DC_TOP_LEFT_SRC_RESNAME;
extern const char* DC_TOP_RIGHT_SRC_RESNAME;

// Use the following with `const auto missionIndexLoaded = (int *)0x9F5E74;` to detect the DSII tunnel run mission.
const int DEATH_STAR_MISSION_INDEX = 52;

// Region names. Used in the erase_region and move_region commands
const int LEFT_RADAR_HUD_BOX_IDX = 0;
const int RIGHT_RADAR_HUD_BOX_IDX = 1;
const int SHIELDS_HUD_BOX_IDX = 2;
const int BEAM_HUD_BOX_IDX = 3;
const int TARGET_HUD_BOX_IDX = 4;
const int LEFT_MSG_HUD_BOX_IDX = 5;
const int RIGHT_MSG_HUD_BOX_IDX = 6;
const int TOP_LEFT_HUD_BOX_IDX = 7;
const int TOP_RIGHT_HUD_BOX_IDX = 8;
const int TEXT_RADIOSYS_HUD_BOX_IDX = 9;
const int TEXT_CMD_HUD_BOX_IDX = 10;
const int MAX_HUD_BOXES = 11;
extern std::vector<const char*>g_HUDRegionNames;
// Convert a string into a *_HUD_BOX_IDX constant
int HUDRegionNameToIndex(char* name);

// Also found in the Floating_GUI_RESNAME list:
extern const char* DC_TARGET_COMP_SRC_RESNAME;
extern const char* DC_LEFT_SENSOR_SRC_RESNAME;
extern const char* DC_LEFT_SENSOR_2_SRC_RESNAME;
extern const char* DC_RIGHT_SENSOR_SRC_RESNAME;
extern const char* DC_RIGHT_SENSOR_2_SRC_RESNAME;
extern const char* DC_SHIELDS_SRC_RESNAME;
extern const char* DC_SOLID_MSG_SRC_RESNAME;
extern const char* DC_BORDER_MSG_SRC_RESNAME;
extern const char* DC_LASER_BOX_SRC_RESNAME;
extern const char* DC_ION_BOX_SRC_RESNAME;
extern const char* DC_BEAM_BOX_SRC_RESNAME;
extern const char* DC_TOP_LEFT_SRC_RESNAME;
extern const char* DC_TOP_RIGHT_SRC_RESNAME;

typedef struct uv_coords_src_dst_struct {
	int src_slot[MAX_DC_COORDS_PER_TEXTURE]; // This src slot references one of the pre-defined DC internal areas
	uvfloat4 dst[MAX_DC_COORDS_PER_TEXTURE];
	uint32_t uBGColor[MAX_DC_COORDS_PER_TEXTURE];
	uint32_t uHGColor[MAX_DC_COORDS_PER_TEXTURE];
	uint32_t uWHColor[MAX_DC_COORDS_PER_TEXTURE];
	int numCoords;
} uv_src_dst_coords;

typedef struct uv_coords_struct {
	uvfloat4 src[MAX_DC_COORDS_PER_TEXTURE];
	int numCoords;
} uv_coords;

const int MAX_TEXTURE_NAME = 128;
typedef struct dc_element_struct {
	uv_src_dst_coords coords;
	int erase_slots[MAX_DC_COORDS_PER_TEXTURE];
	int num_erase_slots;
	char name[MAX_TEXTURE_NAME];
	char coverTextureName[MAX_TEXTURE_NAME];
	//ComPtr<ID3D11ShaderResourceView> coverTexture = nullptr;
	//ID3D11ShaderResourceView *coverTexture = NULL;
	bool bActive, bNameHasBeenTested, bHologram, bNoisyHolo, bTransparent;
} dc_element;

typedef struct move_region_coords_struct {
	int region_slot[MAX_HUD_BOXES];
	uvfloat4 dst[MAX_HUD_BOXES];
	int numCoords;
} move_region_coords;

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
const int MAX_DC_REGIONS = 9;

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
const int SHIELDS_FRONT_DC_ELEM_SRC_IDX = 25;
const int SHIELDS_BACK_DC_ELEM_SRC_IDX = 26;
const int KW_TEXT_CMD_DC_ELEM_SRC_IDX = 27;
const int KW_TEXT_TOP_DC_ELEM_SRC_IDX = 28;
const int KW_TEXT_RADIOSYS_DC_ELEM_SRC_IDX = 29;
const int TEXT_RADIO_DC_ELEM_SRC_IDX = 30;
const int TEXT_SYSTEM_DC_ELEM_SRC_IDX = 31;
const int TEXT_CMD_DC_ELEM_SRC_IDX = 32;
const int TARGETED_OBJ_NAME_SRC_IDX = 33;
const int TARGETED_OBJ_SHD_SRC_IDX = 34;
const int TARGETED_OBJ_HULL_SRC_IDX = 35;
const int TARGETED_OBJ_CARGO_SRC_IDX = 36;
const int TARGETED_OBJ_SYS_SRC_IDX = 37;
const int TARGETED_OBJ_DIST_SRC_IDX = 38;
const int TARGETED_OBJ_SUBCMP_SRC_IDX = 39;
const int MAX_DC_SRC_ELEMENTS = 40;
extern std::vector<const char*>g_DCElemSrcNames;
// Convert a string into a *_DC_ELEM_SRC_IDX constant
int DCSrcElemNameToIndex(char* name);

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

extern dc_element g_DCElements[];
extern int g_iNumDCElements;
extern DCHUDRegions g_DCHUDRegions;
extern DCHUDRegions y;
extern move_region_coords g_DCMoveRegions;
extern char g_sCurrentCockpit[128];
extern bool g_bDCApplyEraseRegionCommands, g_bReRenderMissilesNCounterMeasures;
extern bool g_bEdgeEffectApplied, g_bDCHologramsVisible;
extern float g_fReticleScale;
extern DCElemSrcBoxes g_DCElemSrcBoxes;
//float g_fReticleOfsX = 0.0f;
//float g_fReticleOfsY = 0.0f;
//extern bool g_bInhibitCMDBracket; // Used in XwaDrawBracketHook
//extern float g_fXWAScale;


bool LoadDCInternalCoordinates();