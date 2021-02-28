#include "globals.h"
#include "Materials.h"
#include "utils.h"
#include <string.h>
#include <string>
#include <filesystem>
#include "Vectors.h"
#include "ShadowMapping.h"
#include "DeviceResources.h"

namespace fs = std::filesystem;

bool g_bReloadMaterialsEnabled = false;
Material g_DefaultGlobalMaterial;
GlobalGameEvent g_GameEvent, g_PrevGameEvent;
// Set of flags that tell us when events fired on the current frame
bool bEventsFired[MAX_GAME_EVT] = { 0 };
// Rule 1: The ordering in this array MUST match the ordering in GameEventEnum
// Rule 2: Always begin an event with the token "EVT_" (it's used to parse it)
char *g_sGameEventNames[MAX_GAME_EVT] = {
	"EVT_NONE",
	// Target Events
	"EVT_TARGET_SEL",
	"EVT_LASER_LOCKED",
	"EVT_WARHEAD_LOCKING",
	"EVT_WARHEAD_LOCKED",
	// Cockpit Damage Events
	"EVT_BROKEN_CMD",
	"EVT_BROKEN_LASER_ION",
	"EVT_BROKEN_BEAM_WEAPON",
	"EVT_BROKEN_SHIELDS",
	"EVT_BROKEN_THROTTLE",
	"EVT_BROKEN_SENSORS",
	"EVT_BROKEN_LASER_RECHARGE",
	"EVT_BROKEN_ENGINE_POWER",
	"EVT_BROKEN_SHIELD_RECHARGE",
	"EVT_BROKEN_BEAM_RECHARGE",
	// Hull Damage Events
	"EVT_DAMAGE_75",
	"EVT_DAMAGE_50",
	"EVT_DAMAGE_25",
};


/*
 Contains all the materials for all the OPTs currently loaded
*/
std::vector<CraftMaterials> g_Materials;
// List of all the animated materials used in the current mission.
// This is used to update the timers on these materials.
std::vector<AnimatedTexControl> g_AnimatedMaterials;
// List of all the OPTs seen so far
std::vector<OPTNameType> g_OPTnames;

/*
 * Convert an OPT name into a MAT params file of the form:
 * Materials\<OPTName>.mat
 */
void OPTNameToMATParamsFile(char* OPTName, char* sFileName, int iFileNameSize) {
	snprintf(sFileName, iFileNameSize, "Materials\\%s.mat", OPTName);
}

/*
 * Loads an UV coord row
 */
bool LoadUVCoord(char* buf, Vector2* UVCoord)
{
	int res = 0;
	char* c = NULL;
	float x, y;

	c = strchr(buf, '=');
	if (c != NULL) {
		c += 1;
		try {
			res = sscanf_s(c, "%f, %f", &x, &y);
			if (res < 2) {
				log_debug("[DBG] [MAT] Error (skipping), expected at least 2 elements in '%s'", c);
			}
			UVCoord->x = x;
			UVCoord->y = y;
		}
		catch (...) {
			log_debug("[DBG] [MAT] Could not read 'x, y' from: %s", buf);
			return false;
		}
	}
	return true;
}

/*
 * Loads a Light color row
 */
bool LoadLightColor(char* buf, Vector3* Light)
{
	int res = 0;
	char* c = NULL;
	float r, g, b;

	c = strchr(buf, '=');
	if (c != NULL) {
		c += 1;
		try {
			res = sscanf_s(c, "%f, %f, %f", &r, &g, &b);
			if (res < 3) {
				log_debug("[DBG] [MAT] Error (skipping), expected at least 3 elements in '%s'", c);
			}
			Light->x = r;
			Light->y = g;
			Light->z = b;
		}
		catch (...) {
			log_debug("[DBG] [MAT] Could not read 'r, g, b' from: %s", buf);
			return false;
		}
	}
	return true;
}

#define SKIP_WHITESPACES(s) {\
	while (*s != 0 && *s == ' ') s++;\
	if (*s == 0) return false;\
}

bool ParseEvent(char *s, GameEvent *eventType) {
	char s_event[80];
	int res;
	// Default is EVT_NONE: if no event can be parsed, that's the event we'll assign
	*eventType = EVT_NONE;

	// Parse the event
	try {
		res = sscanf_s(s, "%s", s_event, 80);
		if (res < 1) {
			log_debug("[DBG] [MAT] Error reading event in '%s'", s);
		}
		else {
			log_debug("[DBG] [MAT] EventType: %s", s_event);
			for (int i = 0; i < MAX_GAME_EVT; i++)
				if (stristr(s_event, g_sGameEventNames[i]) != NULL) {
					*eventType = (GameEvent)i;
					break;
				}
		}
	}
	catch (...) {
		log_debug("[DBG] [MAT} Could not read event in '%s'", s);
		return false;
	}
	return true;
}

bool ParseDatFileNameAndGroup(char *buf, char *sDATFileNameOut, int sDATFileNameSize, short *GroupId) {
	std::string s_buf(buf);
	*GroupId = -1;
	sDATFileNameOut[0] = 0;

	int split_idx = s_buf.find_last_of('-');
	if (split_idx > 0) {
		std::string sDATFileName = s_buf.substr(0, split_idx);
		std::string sGroup = s_buf.substr(split_idx + 1);
		try {
			*GroupId = (short)std::stoi(sGroup);
		}
		catch (...) {
			sDATFileNameOut[0] = 0;
			*GroupId = -1;
			return false;
		}
		strcpy_s(sDATFileNameOut, sDATFileNameSize, sDATFileName.c_str());
		return true;
	}
	
	return false;
}

// Parse the DAT file name, GroupId and ImageId from a string of the form:
// <Path>\<DATFileName>-<GroupId>-<ImageId>
// Returns false if the string could not be parsed
bool ParseDatFileNameGroupIdImageId(char *buf, char *sDATFileNameOut, int sDATFileNameSize, short *GroupId, short *ImageId) {
	std::string s_buf(buf);
	*GroupId = -1; *ImageId = -1;
	sDATFileNameOut[0] = 0;
	int split_idx = -1;

	// Find the last hyphen and get the ImageId
	split_idx = s_buf.find_last_of('-');
	if (split_idx < 0)
		return false;
	std::string sImageId = s_buf.substr(split_idx + 1);

	// Terminate the string here and find the next hyphen
	s_buf[split_idx] = 0;
	split_idx = s_buf.find_last_of('-');
	if (split_idx < 0)
		return false;

	std::string sDATFileName = s_buf.substr(0, split_idx);
	std::string sGroupId = s_buf.substr(split_idx + 1);
	try {
		*ImageId = (short)std::stoi(sImageId);
		*GroupId = (short)std::stoi(sGroupId);
	}
	catch (...) {
		sDATFileNameOut[0] = 0;
		*GroupId = -1;
		*ImageId = -1;
		return false;
	}
	strcpy_s(sDATFileNameOut, sDATFileNameSize, sDATFileName.c_str());
	return true;
}

// Interpolates the ImageId between two TexSeqElem's.
// Only call this if the tex sequence is for a DAT file, so that texnames are in the format
// GroupId-ImageId.
void InterpolateTexSequence(std::vector<TexSeqElem> &tex_sequence, TexSeqElem tex_seq_elem0, TexSeqElem tex_seq_elem1)
{
	TexSeqElem cur_tex_seq_elem;
	int ImageId0, ImageId1, ImageIdStep, CurImageId, num_steps;
	float size;
	int res = 0;
	log_debug("[DBG] [MAT] Interpolating tex sequence elements");
	//log_debug("[DBG] [MAT] tex_seq_elem0: [%s], sec: %0.3f, int: %0.3f",
	//	tex_seq_elem0.texname, tex_seq_elem0.seconds, tex_seq_elem0.intensity);

	ImageId0 = tex_seq_elem0.ImageId;
	ImageId1 = tex_seq_elem1.ImageId;
	if (ImageId0 == ImageId1) return; // Nothing to do, both ImageIds are equal and the previous one has already been added.
	ImageIdStep = (ImageId1 > ImageId0) ? 1 : -1;
	num_steps = abs(ImageId1 - ImageId0);
	size = (float )(ImageId1 - ImageId0);
	// The cycle starts at ImageId0 + ImageIdStep because ImageId0 is already part of the sequence:
	// it was read and added in the previous loop in LoadTextureSequence().
	CurImageId = ImageId0 + ImageIdStep;
	for (int i = 0; i < num_steps; i++, CurImageId += ImageIdStep) {
		// Copy the DAT file name:
		strcpy_s(cur_tex_seq_elem.texname, MAX_TEX_SEQ_NAME, tex_seq_elem0.texname);
		// Interpolate the seconds and intensity attributes:
		cur_tex_seq_elem.seconds = tex_seq_elem1.seconds * (CurImageId - ImageId0) / size + tex_seq_elem0.seconds * (ImageId1 - CurImageId) / size;
		cur_tex_seq_elem.intensity = tex_seq_elem1.intensity * (CurImageId - ImageId0) / size + tex_seq_elem0.intensity * (ImageId1 - CurImageId) / size;
		// Fill the rest of the fields:
		cur_tex_seq_elem.IsDATImage = true;
		cur_tex_seq_elem.GroupId = tex_seq_elem0.GroupId;
		cur_tex_seq_elem.ImageId = CurImageId;
		cur_tex_seq_elem.ExtraTextureIndex = -1;
		log_debug("[DBG] [MAT] Adding interpolated elem: %d-%d, sec: %0.3f, int: %0.3f",
			cur_tex_seq_elem.GroupId, cur_tex_seq_elem.ImageId, cur_tex_seq_elem.seconds, cur_tex_seq_elem.intensity);
		tex_sequence.push_back(cur_tex_seq_elem);
	}
}

int AlphaModeToSpecialControl(int alpha_mode) {
	switch (alpha_mode) {
	case 1:
		return SPECIAL_CONTROL_BLACK_TO_ALPHA;
		break;
	case 2:
		return SPECIAL_CONTROL_ALPHA_IS_BLOOM_MASK;
		break;
	default:
		return 0;
	}
}

bool ParseOptionalTexSeqArgs(char *buf, int *alpha_mode, float4 *AuxColor)
{
	int res = 0, raw_alpha_mode;
	float r, g, b;

	*alpha_mode = 0;
	AuxColor->x = 1.0f;
	AuxColor->y = 1.0f;
	AuxColor->z = 1.0f;
	AuxColor->w = 1.0f;
	res = sscanf_s(buf, "%d, [%f, %f, %f]", &raw_alpha_mode, &r, &g, &b);
	if (res < 4)
	{
		log_debug("[DBG] [MAT] Error, expected at least 4 elements reading OptTexSeqArgs in %s", buf);
		return false;
	}
	*alpha_mode = AlphaModeToSpecialControl(raw_alpha_mode);
	AuxColor->x = r;
	AuxColor->y = g;
	AuxColor->z = b;
	AuxColor->w = 1.0f;
	return true;
}

/*
 Load a texture sequence line of the form:

 lightmap_seq|rand|anim_seq|rand = [Event], [DATFileName], <TexName1>,<seconds1>,<intensity1>, <TexName2>,<seconds2>,<intensity2>, ... 

*/
bool LoadTextureSequence(char *buf, std::vector<TexSeqElem> &tex_sequence, GameEvent *eventType, int *alpha_mode,
	float4 *AuxColor) 
{
	TexSeqElem tex_seq_elem, prev_tex_seq_elem;
	int res = 0;
	char temp[512];
	char *s = NULL, *t = NULL, texname[80], sDATFileName[128], sOptionalArgs[128];
	float seconds, intensity = 1.0f;
	bool IsDATFile = false, bEllipsis = false;
	*eventType = EVT_NONE;
	std::string s_temp;
	*alpha_mode = 0;
	AuxColor->x = 1.0f;
	AuxColor->y = 1.0f;
	AuxColor->z = 1.0f;
	AuxColor->w = 1.0f;
	
	// Remove any parentheses from the line
	int i = 0, j = 0;
	while (buf[i] && j < 511) {
		if (buf[i] != '(' && buf[i] != ')')
			temp[j++] = buf[i];
		i++;
	}
	temp[j] = 0;
	
	//log_debug("[DBG] [MAT] Reading texture sequence");
	s = strchr(temp, '=');
	if (s == NULL) return false;

	// Skip the equals sign:
	s += 1;

	// Parse the event, if it exists
	if (stristr(s, "EVT_") != NULL) {
		// Skip to the next comma
		t = s;
		while (*t != 0 && *t != ',') t++;
		// If we reached the end of the string, that's an error
		if (*t == 0) return false;
		// End this string on the comma so that we can parse a string
		*t = 0;
		ParseEvent(s, eventType);
		// Skip the comma
		s = t; s += 1;
		SKIP_WHITESPACES(s);
	}

	// Parse the DAT file name, if specified
	if (stristr(s, ".dat") != NULL) {
		IsDATFile = true;
		SKIP_WHITESPACES(s);
		// Skip to the next comma
		t = s;
		while (*t != 0 && *t != ',') t++;
		// If we reached the end of the string, that's an error
		if (*t == 0) return false;
		// End this string on the comma so that we can parse a string
		*t = 0;
		// Copy the DAT's filename
		strcpy_s(sDATFileName, 128, s);
		// Skip the comma
		s = t; s += 1;
		SKIP_WHITESPACES(s);
	}

	// Parse optional args, if specified
	if (s[0] == '{') {
		// Skip the opening braces
		s++;
		// Skip to the closing braces
		t = s;
		while (*t != 0 && *t != '}') t++;
		// If we reached the end of the string, that's an error
		if (*t == 0) return false;
		// End this string on the closing braces so that we can parse a string
		*t = 0;
		// Copy the optional args
		strcpy_s(sOptionalArgs, 128, s);
		// log_debug("[DBG] [MAT] sOptionalArgs: [%s]", sOptionalArgs);
		// Parse the optional args...
		ParseOptionalTexSeqArgs(sOptionalArgs, alpha_mode, AuxColor);
		// Skip the closing braces
		s = t; s += 1;
		// Skip to the next comma
		t = s;
		while (*t != 0 && *t != ',') t++;
		// If we reached the end of the string, that's an error
		if (*t == 0) return false;
		// Skip the comma and remaining white spaces
		s = t; s += 1;
		SKIP_WHITESPACES(s);
	}

	while (*s != 0)
	{
		// Skip to the next comma, we want to read the texname
		t = s;
		while (*t != 0 && *t != ',') t++;
		// If we reached the end of the string, that's an error
		if (*t == 0) return false;
		// End this string on the comma so that we can parse a string
		*t = 0;
		// Parse the texture name/GroupId-ImageId
		try {
			res = sscanf_s(s, "%s", texname, 80);
			if (res < 1) log_debug("[DBG] [MAT] Error reading texname in '%s'", s);
		}
		catch (...) {
			log_debug("[DBG] [MAT] Could not read texname in '%s'", s);
			return false;
		}

		// Skip the comma
		s = t; s += 1;
		SKIP_WHITESPACES(s);

		// if texname happens to be an ellipsis, then we didn't read the texname, so
		// we need to do it again
		if (strstr(texname, "..") != NULL) {
			texname[0] = 0;
			bEllipsis = true;
			// Skip to the next comma, we want to read the texname
			t = s;
			while (*t != 0 && *t != ',') t++;
			// If we reached the end of the string, that's an error
			if (*t == 0) return false;
			// End this string on the comma so that we can parse a string
			*t = 0;
			// Parse the texture name/GroupId-ImageId
			try {
				res = sscanf_s(s, "%s", texname, 80);
				if (res < 1) log_debug("[DBG] [MAT] Error reading texname in '%s'", s);
			}
			catch (...) {
				log_debug("[DBG] [MAT] Could not read texname in '%s'", s);
				return false;
			}

			// Skip the comma
			s = t; s += 1;
			SKIP_WHITESPACES(s);
		}
		else {
			bEllipsis = false;
		}

		// Parse the seconds
		t = s;
		while (*t != 0 && *t != ',')
			t++;
		// If we reached the end of the string, that's an error
		if (*t == 0)
			return false;
		try {
			res = sscanf_s(s, "%f", &seconds);
			if (res < 1) log_debug("[DBG] [MAT] Error reading seconds in '%s'", s);
		}
		catch (...) {
			log_debug("[DBG] [MAT] Could not read seconds in '%s'", s);
			return false;
		}

		// Skip the comma
		s = t; s += 1;
		SKIP_WHITESPACES(s);

		// Parse the intensity
		t = s;
		while (*t != 0 && *t != ',')
			t++;
		try {
			res = sscanf_s(s, "%f", &intensity);
			if (res < 1) log_debug("[DBG] [MAT] Error reading intensity in '%s'", s);
		}
		catch (...) {
			log_debug("[DBG] [MAT] Could not read intensity in '%s'", s);
			return false;
		}

		// Save the last tex_seq_elem, we might need it later, to interpolate a range of elems.
		prev_tex_seq_elem = tex_seq_elem;
		// Populate the tex_seq_elem with the data we just read
		if (IsDATFile) {
			// This is a DAT file sequence, texname should be parsed as GroupId-ImageId
			try {
				int GroupId = -1, ImageId = -1;
				int res = sscanf_s(texname, "%d-%d", &GroupId, &ImageId);
				if (res < 2) {
					log_debug("[DBG] [MAT] Could not parse GroupId-ImageId from [%s]", texname);
					return false;
				}
				strcpy_s(tex_seq_elem.texname, MAX_TEX_SEQ_NAME, sDATFileName);
				tex_seq_elem.seconds = seconds;
				tex_seq_elem.intensity = intensity;
				tex_seq_elem.IsDATImage = true;
				tex_seq_elem.GroupId = GroupId;
				tex_seq_elem.ImageId = ImageId;
				// The texture itself will be loaded later. So the reference index is initialized to -1 here:
				tex_seq_elem.ExtraTextureIndex = -1;
				if (!bEllipsis) {
					tex_sequence.push_back(tex_seq_elem);
					log_debug("[DBG] DAT tex_seq_elem added: [%s], Group: %d, ImageId: %d",
						tex_seq_elem.texname, GroupId, ImageId);
				}
				else {
					// Interpolate between prev_tex_seq_elem and tex_seq_elem
					InterpolateTexSequence(tex_sequence, prev_tex_seq_elem, tex_seq_elem);
				}
			}
			catch (...) {
				log_debug("[DBG] ERROR: Could not parse [%s] as an ImageId", texname);
				return false;
			}
		}
		else {
			strcpy_s(tex_seq_elem.texname, MAX_TEX_SEQ_NAME, texname);
			tex_seq_elem.seconds = seconds;
			tex_seq_elem.intensity = intensity;
			tex_seq_elem.IsDATImage = false;
			// The texture itself will be loaded later. So the reference index is initialized to -1 here:
			tex_seq_elem.ExtraTextureIndex = -1;
			tex_sequence.push_back(tex_seq_elem);
		}

		// If we reached the end of the string, we're done
		if (*t == 0) break;
		// Else, we need to parse the next texture...
		// Skip the current char (which should be a comma) and repeat
		s = t; s += 1;
	}
	//log_debug("[DBG] [MAT] Texture sequence done");
	return true;
}

bool LoadFrameSequence(char *buf, std::vector<TexSeqElem> &tex_sequence, GameEvent *eventType,
	int *is_lightmap, int *alpha_mode, float4 *AuxColor, float2 *Offset, float *AspectRatio, int *Clamp) 
{
	int res = 0, clamp, raw_alpha_mode;
	char *s = NULL, *t = NULL, path[256];
	float fps = 30.0f, intensity = 0.0f, r,g,b, OfsX, OfsY, ar;
	*is_lightmap = 0; *alpha_mode = 0; *eventType = EVT_NONE;
	AuxColor->x = 1.0f;
	AuxColor->y = 1.0f;
	AuxColor->z = 1.0f;
	AuxColor->w = 1.0f;
	Offset->x = 0.0f; Offset->y = 0.0f;
	*AspectRatio = 1.0f; *Clamp = 0;

	//log_debug("[DBG] [MAT] Reading texture sequence");
	s = strchr(buf, '=');
	if (s == NULL) return false;

	// Skip the equals sign:
	s += 1;

	// Skip to the next comma
	t = s;
	while (*t != 0 && *t != ',') t++;
	// If we reached the end of the string, that's an error
	if (*t == 0) return false;
	// End this string on the comma so that we can parse a string
	*t = 0;
	ParseEvent(s, eventType);

	// Skip the comma
	s = t; s += 1;
	SKIP_WHITESPACES(s);

	// Skip to the next comma
	t = s;
	while (*t != 0 && *t != ',') t++;
	// If we reached the end of the string, that's an error
	if (*t == 0) return false;
	// End this string on the comma so that we can parse a string
	*t = 0;
	// Parse the texture name
	try {
		res = sscanf_s(s, "%s", path, 256);
		if (res < 1) log_debug("[DBG] [MAT] Error reading path in '%s'", s);
	}
	catch (...) {
		log_debug("[DBG] [MAT] Could not read path in '%s'", s);
		return false;
	}

	// Skip the comma
	s = t; s += 1;
	SKIP_WHITESPACES(s);

	// Parse the remaining fields: fps, lightmap, intensity, black-to-alpha
	try {
		res = sscanf_s(s, "%f, %d, %f, %d; [%f, %f, %f], %f, (%f, %f), %d", 
			&fps, is_lightmap, &intensity, &raw_alpha_mode,
			&r, &g, &b, &ar, &OfsX, &OfsY, &clamp);
		if (res < 4) {
			log_debug("[DBG] [MAT] Error (using defaults), expected at least 4 elements in %s", s);
		}

		*alpha_mode = AlphaModeToSpecialControl(raw_alpha_mode);
		if (res >= 7) {
			AuxColor->x = r;
			AuxColor->y = g;
			AuxColor->z = b;
			AuxColor->w = 1.0f;
		}
		if (res >= 8)
			*AspectRatio = ar; // Aspect Ratio
		if (res >= 10) {
			Offset->x = OfsX;
			Offset->y = OfsY;
		}
		if (res >= 11)
			*Clamp = clamp; // clamp uv
		
	}
	catch (...) {
		log_debug("[DBG] [MAT] Could not read (fps, lightmap, intensity, black-to-alpha) from %s", s);
		return false;
	}

	TexSeqElem tex_seq_elem;
	// The path is either an actual path that contains the frame sequence, or it's
	// a <Path>\<DATFile>-<GroupId> or <Path>\<DATFile>-<GroupId>-<ImageId>. Let's
	// check if path contains the ".DAT-" token first:
	char *split = stristr(path, ".dat-");
	if (split != NULL) {
		// Split the string at the first dash and move the cursor after it:
		split[4] = 0;
		split += 5;
		// sDATFileName contains the path and DAT file name:
		std::string sDATFileName(path);
		std::string sGroupImage(split);
		std::string sGroup, sImage;
		int split_idx = sGroupImage.find_last_of('-');
		if (split_idx > 0) {
			sGroup = sGroupImage.substr(0, split_idx);
			sImage = sGroupImage.substr(split_idx + 1);
		}
		else {
			sGroup = sGroupImage;
			sImage = "";
		}
		log_debug("[DBG] sDATFileName: [%s], Group: [%s], Image: [%s]", sDATFileName.c_str(), sGroup.c_str(), sImage.c_str());

		short GroupId = -1, ImageId = -1;
		try {
			GroupId = (short)std::stoi(sGroup);
		}
		catch (...) {
			log_debug("[DBG] Could not parse: %s into an integer", sGroup.c_str());
			return false;
		}
		if (sImage.size() > 0) {
			try {
				ImageId = (short)std::stoi(sImage);
			}
			catch (...) {
				log_debug("[DBG] Could not parse: %s into an integer", sImage.c_str());
				return false;
			}
		}

		std::vector<short> ImageList;
		if (ImageId > -1) {
			ImageList.push_back(ImageId);
			log_debug("[DBG] Using only %d-%d", GroupId, ImageId);
		}
		else {
			ImageList = ReadDATImageListFromGroup(sDATFileName.c_str(), GroupId);
			log_debug("[DBG] Group %d has %d images", GroupId, ImageList.size());
		}
		// Iterate over the list of Images and add one TexSeqElem for each one of them
		for each (short ImageId in ImageList)
		{
			// Store the DAT filename in texname and set the appropriate flag. texname contains
			// the path and filename.
			strcpy_s(tex_seq_elem.texname, MAX_TEX_SEQ_NAME, sDATFileName.c_str());
			tex_seq_elem.seconds = 1.0f / fps;
			tex_seq_elem.intensity = intensity;
			// Save the DAT image data:
			tex_seq_elem.IsDATImage = true;
			tex_seq_elem.GroupId = GroupId;
			tex_seq_elem.ImageId = ImageId;
			// The texture itself will be loaded later. So the reference index is initialized to -1 here:
			tex_seq_elem.ExtraTextureIndex = -1;
			tex_sequence.push_back(tex_seq_elem);
		}
		ImageList.clear();
	}
	else {
		std::string s_path = std::string("Effects\\Animations\\") + std::string(path);
		if (fs::is_directory(s_path)) {
			// We just finished reading the path where a frame sequence is stored
			// Now we need to load all the frames in that path into a tex_seq_elem

			//log_debug("[DBG] Listing files under path: %s", s_path.c_str());
			for (const auto & entry : fs::directory_iterator(s_path)) {
				// Surely there's a better way to get the filename...
				std::string filename = std::string(path) + "\\" + entry.path().filename().string();
				//log_debug("[DBG] file: %s", filename.c_str());

				strcpy_s(tex_seq_elem.texname, MAX_TEX_SEQ_NAME, filename.c_str());
				tex_seq_elem.IsDATImage = false;
				tex_seq_elem.seconds = 1.0f / fps;
				tex_seq_elem.intensity = intensity;
				// The texture itself will be loaded later. So the reference index is initialized to -1 here:
				tex_seq_elem.ExtraTextureIndex = -1;
				tex_sequence.push_back(tex_seq_elem);
			}
		}
	}
	
	return true;
}

inline void AssignTextureEvent(GameEvent eventType, Material* curMaterial, int ATCType=TEXTURE_ATC_IDX)
{
	curMaterial->TextureATCIndices[ATCType][eventType] = g_AnimatedMaterials.size() - 1;
}

void ReadMaterialLine(char* buf, Material* curMaterial) {
	char param[256], svalue[512];
	float fValue = 0.0f;

	// Skip comments and blank lines
	if (buf[0] == ';' || buf[0] == '#')
		return;
	if (strlen(buf) == 0)
		return;

	// Read the parameter
	if (sscanf_s(buf, "%s = %s", param, 256, svalue, 512) > 0) {
		fValue = (float)atof(svalue);
	}

	if (_stricmp(param, "Metallic") == 0) {
		//log_debug("[DBG] [MAT] Metallic: %0.3f", fValue);
		curMaterial->Metallic = fValue;
	}
	else if (_stricmp(param, "Intensity") == 0) {
		//log_debug("[DBG] [MAT] Intensity: %0.3f", fValue);
		curMaterial->Intensity = fValue;
	}
	else if (_stricmp(param, "Glossiness") == 0) {
		//log_debug("[DBG] [MAT] Glossiness: %0.3f", fValue);
		curMaterial->Glossiness = fValue;
	}
	else if (_stricmp(param, "NMIntensity") == 0) {
		curMaterial->NMIntensity = fValue;
	}
	else if (_stricmp(param, "SpecularVal") == 0) {
		curMaterial->SpecValue = fValue;
	}
	else if (_stricmp(param, "Shadeless") == 0) {
		curMaterial->IsShadeless = (bool)fValue;
		log_debug("[DBG] Shadeless texture loaded");
	}
	else if (_stricmp(param, "Light") == 0) {
		LoadLightColor(buf, &(curMaterial->Light));
		//log_debug("[DBG] [MAT] Light: %0.3f, %0.3f, %0.3f",
		//	curMaterialTexDef.material.Light.x, curMaterialTexDef.material.Light.y, curMaterialTexDef.material.Light.z);
	}
	else if (_stricmp(param, "light_uv_coord_pos") == 0) {
		LoadUVCoord(buf, &(curMaterial->LightUVCoordPos));
		//log_debug("[DBG] [MAT] LightUVCoordPos: %0.3f, %0.3f",
		//	curMaterial->LightUVCoordPos.x, curMaterial->LightUVCoordPos.y);
	}
	else if (_stricmp(param, "NoBloom") == 0) {
		curMaterial->NoBloom = (bool)fValue;
		log_debug("[DBG] NoBloom: %d", curMaterial->NoBloom);
	}
	// Shadow Mapping settings
	else if (_stricmp(param, "shadow_map_mult_x") == 0) {
		g_ShadowMapping.shadow_map_mult_x = fValue;
		log_debug("[DBG] [SHW] shadow_map_mult_x: %0.3f", fValue);
	}
	else if (_stricmp(param, "shadow_map_mult_y") == 0) {
		g_ShadowMapping.shadow_map_mult_y = fValue;
		log_debug("[DBG] [SHW] shadow_map_mult_y: %0.3f", fValue);
	}
	else if (_stricmp(param, "shadow_map_mult_z") == 0) {
		g_ShadowMapping.shadow_map_mult_z = fValue;
		log_debug("[DBG] [SHW] shadow_map_mult_z: %0.3f", fValue);
	}
	// Lava Settings
	else if (_stricmp(param, "Lava") == 0) {
		curMaterial->IsLava = (bool)fValue;
	}
	else if (_stricmp(param, "LavaSpeed") == 0) {
		curMaterial->LavaSpeed = fValue;
	}
	else if (_stricmp(param, "LavaSize") == 0) {
		curMaterial->LavaSize = fValue;
	}
	else if (_stricmp(param, "EffectBloom") == 0) {
		// Used for specific effects where specifying bloom is difficult, like the Lava
		// and the AlphaToBloom shaders.
		curMaterial->EffectBloom = fValue;
	}
	else if (_stricmp(param, "LavaColor") == 0) {
		LoadLightColor(buf, &(curMaterial->LavaColor));
	}
	else if (_stricmp(param, "LavaTiling") == 0) {
		curMaterial->LavaTiling = (bool)fValue;
	}
	else if (_stricmp(param, "AlphaToBloom") == 0) {
		// Uses the color alpha to apply bloom. Can be used to force surfaces to glow
		// when they don't have an illumination texture.
		curMaterial->AlphaToBloom = (bool)fValue;
	}
	else if (_stricmp(param, "NoColorAlpha") == 0) {
		// Forces color alpha to 0. Only takes effect when AlphaToBloom is set.
		curMaterial->NoColorAlpha = (bool)fValue;
	}
	else if (_stricmp(param, "AlphaIsntGlass") == 0) {
		// When set, semi-transparent areas aren't converted to glass
		curMaterial->AlphaIsntGlass = (bool)fValue;
	}
	else if (_stricmp(param, "Ambient") == 0) {
		// Additional ambient component. Only used in PixelShaderNoGlass
		curMaterial->Ambient = fValue;
	}
	else if (_stricmp(param, "TotalFrames") == 0) {
		curMaterial->TotalFrames = (int)fValue;
	}
	else if (_stricmp(param, "ExplosionScale") == 0) {
		// The user-facing explosion scale must be translated into the scale that the
		// ExplosionShader users. For some reason (the code isn't mine) the scale in
		// the shader works like this:
		// ExplosionScale: 4.0 == small, 2.0 == normal, 1.0 == big.
		// So the formula is:
		// ExplosionScale = 4.0 / UserExplosionScale
		// Because:
		// 4.0 / UserExplosionScale yields:
		// 4.0 / 1.0 -> 4 = small
		// 4.0 / 2.0 -> 2 = medium
		// 4.0 / 4.0 -> 1 = big
		float UserExplosionScale = max(0.5f, fValue); // Disallow values smaller than 0.5
		curMaterial->ExplosionScale = 4.0f / UserExplosionScale;
	}
	else if (_stricmp(param, "ExplosionSpeed") == 0) {
		curMaterial->ExplosionSpeed = fValue;
	}
	else if (_stricmp(param, "ExplosionBlendMode") == 0) {
		// 0: No Blending, use the original texture
		// 1: Blend with the original texture
		// 2: Replace original texture with procedural explosion
		curMaterial->ExplosionBlendMode = (int)fValue;
	} 
	else if (_stricmp(param, "lightmap_seq") == 0 ||
			 _stricmp(param, "lightmap_rand") == 0 ||
			 _stricmp(param, "lightmap_once") == 0 ||
			 _stricmp(param, "anim_seq") == 0 ||
			 _stricmp(param, "anim_rand") == 0 ||
			 _stricmp(param, "anim_once") == 0) {
		// TOOD: Add support for multiline sequences
		AnimatedTexControl atc;
		int alpha_mode = 0;
		atc.IsRandom = (_stricmp(param, "lightmap_rand") == 0) || (_stricmp(param, "anim_rand") == 0);
		bool IsLightMap = (_stricmp(param, "lightmap_rand") == 0) || (_stricmp(param, "lightmap_seq") == 0);
		atc.NoLoop = _stricmp(param, "lightmap_once") == 0 || _stricmp(param, "anim_once") == 0;
		// Clear the current Sequence and release the associated textures in the DeviceResources...
		// TODO: Either release the ExtraTextures pointed at by LightMapSequence, or garbage-collect them
		//       later...
		atc.Sequence.clear();
		log_debug("[DBG] [MAT] Loading Animated LightMap/Texture data for [%s]", buf);
		if (!LoadTextureSequence(buf, atc.Sequence, &(atc.Event), &alpha_mode, &(atc.Tint)))
			log_debug("[DBG] [MAT] Error loading animated LightMap/Texture data for [%s], syntax error?", buf);
		if (atc.Sequence.size() > 0) {
			log_debug("[DBG] [MAT] Sequence.size() = %d for Texture [%s]", atc.Sequence.size(), buf);
			// Initialize the timer for this animation
			atc.ResetAnimation();
			atc.BlackToAlpha = false;
			atc.AlphaIsBloomMask = false;
			switch (alpha_mode) {
			case SPECIAL_CONTROL_BLACK_TO_ALPHA:
				atc.BlackToAlpha = true;
				break;
			case SPECIAL_CONTROL_ALPHA_IS_BLOOM_MASK:
				atc.AlphaIsBloomMask = true;
				break;
			}
			// Add a reference to this material on the list of animated materials
			g_AnimatedMaterials.push_back(atc);
			AssignTextureEvent(atc.Event, curMaterial, IsLightMap ? LIGHTMAP_ATC_IDX : TEXTURE_ATC_IDX);

			/*
			log_debug("[DBG] [MAT] >>>>>> Animation Sequence Start");
			for each (TexSeqElem t in atc.LightMapSequence) {
				log_debug("[DBG] [MAT] <%s>, %0.3f, %0.3f", t.texname, t.seconds, t.intensity);
			}
			log_debug("[DBG] [MAT] <<<<<< Animation Sequence End");
			*/
		}
		else {
			log_debug("[DBG] [MAT] ERROR: No Animation Data Loaded for [%s]", buf);
		}
	}
	else if (_stricmp(param, "frame_seq") == 0 ||
			 _stricmp(param, "frame_rand") == 0 ||
			 _stricmp(param, "frame_once") == 0) {
		AnimatedTexControl atc;
		int is_lightmap, alpha_mode;
		atc.IsRandom = _stricmp(param, "frame_rand") == 0;
		atc.NoLoop = _stricmp(param, "frame_once") == 0;
		// Clear the current Sequence and release the associated textures in the DeviceResources...
		// TODO: Either release the ExtraTextures pointed at by LightMapSequence, or garbage-collect them
		//       later...
		atc.Sequence.clear();
		log_debug("[DBG] [MAT] Loading Frame Sequence data for [%s]", buf);
		if (!LoadFrameSequence(buf, atc.Sequence, &(atc.Event), &is_lightmap, &alpha_mode,
			&(atc.Tint), &(atc.Offset), &(atc.AspectRatio), &(atc.Clamp)))
			log_debug("[DBG] [MAT] Error loading animated LightMap/Texture data for [%s], syntax error?", buf);
		if (atc.Sequence.size() > 0) {
			log_debug("[DBG] [MAT] Sequence.size() = %d for Texture [%s]", atc.Sequence.size(), buf);
			// Initialize the timer for this animation
			atc.ResetAnimation();
			atc.BlackToAlpha = false;
			atc.AlphaIsBloomMask = false;
			switch (alpha_mode) {
			case SPECIAL_CONTROL_BLACK_TO_ALPHA:
				atc.BlackToAlpha = true;
				break;
			case SPECIAL_CONTROL_ALPHA_IS_BLOOM_MASK:
				atc.AlphaIsBloomMask = true;
				break;
			}
			// Add a reference to this material on the list of animated materials
			g_AnimatedMaterials.push_back(atc);
			AssignTextureEvent(atc.Event, curMaterial, is_lightmap ? LIGHTMAP_ATC_IDX : TEXTURE_ATC_IDX);
		}
		else {
			log_debug("[DBG] [MAT] ERROR: No Animation Data Loaded for [%s]", buf);
		}
	}
	/*
	else if (_stricmp(param, "LavaNormalMult") == 0) {
		LoadLightColor(buf, &(curMaterial->LavaNormalMult));
		log_debug("[DBG] [MAT] LavaNormalMult: %0.3f, %0.3f, %0.3f",
			curMaterial->LavaNormalMult.x, curMaterial->LavaNormalMult.y, curMaterial->LavaNormalMult.z);
	}
	else if (_stricmp(param, "LavaPosMult") == 0) {
		LoadLightColor(buf, &(curMaterial->LavaPosMult));
		log_debug("[DBG] [MAT] LavaPosMult: %0.3f, %0.3f, %0.3f",
			curMaterial->LavaPosMult.x, curMaterial->LavaPosMult.y, curMaterial->LavaPosMult.z);
	}
	else if (_stricmp(param, "LavaTranspose") == 0) {
		curMaterial->LavaTranspose = (bool)fValue;
	}
	*/
}

/*
 * Load the material parameters for an individual OPT or DAT
 */
bool LoadIndividualMATParams(char *OPTname, char *sFileName, bool verbose) {
	// I may have to use std::array<char, DIM> and std::vector<std::array<char, Dim>> instead
	// of TexnameType
	// https://stackoverflow.com/questions/21829451/push-back-on-a-vector-of-array-of-char
	FILE* file;
	int error = 0, line = 0;

	try {
		error = fopen_s(&file, sFileName, "rt");
	}
	catch (...) {
		//log_debug("[DBG] [MAT] Could not load [%s]", sFileName);
	}

	if (error != 0) {
		//log_debug("[DBG] [MAT] Error %d when loading [%s]", error, sFileName);
		return false;
	}

	if (verbose) log_debug("[DBG] [MAT] Loading Craft Material params for [%s]...", sFileName);
	char buf[512], param[256], svalue[512]; // texname[MAX_TEXNAME];
	std::vector<TexnameType> texnameList;
	int param_read_count = 0;
	float fValue = 0.0f;
	MaterialTexDef curMaterialTexDef;

	// Find this OPT in the global materials and clear it if necessary...
	int craftIdx = FindCraftMaterial(OPTname);
	if (craftIdx < 0) {
		// New Craft Material
		if (verbose) log_debug("[DBG] [MAT] New Craft Material (%s)", OPTname);
		//craftIdx = g_Materials.size();
	}
	else {
		// Existing Craft Material, clear it
		if (verbose) log_debug("[DBG] [MAT] Existing Craft Material, clearing %s", OPTname);
		g_Materials[craftIdx].MaterialList.clear();
	}
	CraftMaterials craftMat;
	// Clear the materials for this craft and add a default material
	craftMat.MaterialList.clear();
	strncpy_s(craftMat.OPTname, OPTname, MAX_OPT_NAME);

	curMaterialTexDef.material = g_DefaultGlobalMaterial;
	strncpy_s(curMaterialTexDef.texname, "Default", MAX_TEXNAME);
	// The default material will always be in slot 0:
	craftMat.MaterialList.push_back(curMaterialTexDef);
	//craftMat.MaterialList.insert(craftMat.MaterialList.begin(), materialTexDef);

	// We always start the craft material with one material: the default material in slot 0
	bool MaterialSaved = true;
	texnameList.clear();
	while (fgets(buf, 512, file) != NULL) {
		line++;
		// Skip comments and blank lines
		if (buf[0] == ';' || buf[0] == '#')
			continue;
		if (strlen(buf) == 0)
			continue;
		
		if (sscanf_s(buf, "%s = %s", param, 256, svalue, 512) > 0) {
			fValue = (float)atof(svalue);

			if (buf[0] == '[') {

				//strcpy_s(texname, MAX_TEXNAME, buf + 1);
				// Get rid of the trailing ']'
				//char *end = strstr(texname, "]");
				//if (end != NULL)
				//	*end = 0;
				//log_debug("[DBG] [MAT] texname: %s", texname);

				if (!MaterialSaved) {
					// There's an existing material that needs to be saved before proceeding
					for (TexnameType texname : texnameList) {
						// Copy the texture name from the list to the current material
						strncpy_s(curMaterialTexDef.texname, texname.name, MAX_TEXNAME);
						// Special case: overwrite the default material
						if (_stricmp("default", curMaterialTexDef.texname) == 0) {
							//log_debug("[DBG] [MAT] Overwriting the default material for this craft");
							craftMat.MaterialList[0] = curMaterialTexDef;
						}
						else {
							//log_debug("[DBG] [MAT] Adding new material: %s", curMaterialTexDef.texname);
							craftMat.MaterialList.push_back(curMaterialTexDef);
						}
					}
				}
				texnameList.clear();

				// Extract the name(s) of the texture(s)
				{
					char* start = buf + 1; // Skip the '[' bracket
					char* end;
					while (*start && *start != ']') {
						end = start;
						// Skip chars until we find ',' ']' or EOS
						while (*end && *end != ',' && *end != ']')
							end++;
						// If we didn't hit EOS, then add the token to the list
						if (*end)
						{
							TexnameType texname;
							strncpy_s(texname.name, start, end - start);
							//log_debug("[DBG] [MAT] Adding texname: [%s]", texname.texname);
							texnameList.push_back(texname);
							start = end + 1;
						}
						else
							start = end;
					}

					// DEBUG
					/*log_debug("[DBG] [MAT] ---------------");
					for (uint32_t i = 0; i < texnameList.size(); i++)
						log_debug("[DBG] [MAT] %s, ", texnameList[i].name);
					log_debug("[DBG] [MAT] ===============");*/
					// DEBUG
				}

				// Start a new material
				//strncpy_s(curMaterialTexDef.texname, texname, MAX_TEXNAME);
				curMaterialTexDef.texname[0] = 0;
				curMaterialTexDef.material = g_DefaultGlobalMaterial;
				MaterialSaved = false;
			}
			else
				ReadMaterialLine(buf, &(curMaterialTexDef.material));
		}
	}
	fclose(file);

	// Save the last material if necessary...
	if (!MaterialSaved) {
		// There's an existing material that needs to be saved before proceeding
		for (TexnameType texname : texnameList) {
			// Copy the texture name from the list to the current material
			strncpy_s(curMaterialTexDef.texname, texname.name, MAX_TEXNAME);
			// Special case: overwrite the default material
			if (_stricmp("default", curMaterialTexDef.texname) == 0) {
				//log_debug("[DBG] [MAT] (last) Overwriting the default material for this craft");
				craftMat.MaterialList[0] = curMaterialTexDef;
			}
			else {
				//log_debug("[DBG] [MAT] (last) Adding new material: %s", curMaterialTexDef.texname);
				craftMat.MaterialList.push_back(curMaterialTexDef);
			}
		}
	}
	texnameList.clear();

	// Replace the craft material in g_Materials
	if (craftIdx < 0) {
		//log_debug("[DBG] [MAT] Adding new craft material %s", OPTname);
		g_Materials.push_back(craftMat);
		craftIdx = g_Materials.size() - 1;
	}
	else {
		//log_debug("[DBG] [MAT] Replacing existing craft material %s", OPTname);
		g_Materials[craftIdx] = craftMat;
	}

	// DEBUG
	// Print out the materials for this craft
	/*log_debug("[DBG] [MAT] *********************");
	log_debug("[DBG] [MAT] Craft Materials for OPT: %s", g_Materials[craftIdx].OPTname);
	for (uint32_t i = 0; i < g_Materials[craftIdx].MaterialList.size(); i++) {
		Material defMat = g_Materials[craftIdx].MaterialList[i].material;
		log_debug("[DBG] [MAT] %s, M:%0.3f, I:%0.3f, G:%0.3f",
			g_Materials[craftIdx].MaterialList[i].texname,
			defMat.Metallic, defMat.Intensity, defMat.Glossiness);
	}
	log_debug("[DBG] [MAT] *********************");*/
	// DEBUG
	return true;
}

bool GetGroupIdImageIdFromDATName(char* DATName, int* GroupId, int* ImageId) {
	// Extract the Group Id and Image Id from the dat name:
	// Sample dat name:
	// [dat,9002,1200,0,0]
	char* idx = strstr(DATName, "dat,");
	if (idx == NULL) {
		log_debug("[DBG] [MAT] Could not find 'dat,' substring in [%s]", DATName);
		return false;
	}
	idx += 4; // Skip the "dat," token, we're now pointing to the first char of the Group Id
	*GroupId = atoi(idx); // Convert the current string to the Group Id

	// Advance to the next comma
	idx = strstr(idx, ",");
	if (idx == NULL) {
		log_debug("[DBG] [MAT] Error while parsing [%s], could not find ImageId", DATName);
		return false;
	}
	idx += 1; // Skip the comma
	*ImageId = atoi(idx); // Convert the current string to the Image Id
	return true;
}

/*
 * Convert a DAT name into two MAT params files of the form:
 *
 * sFileName:		Materials\dat-<GroupId>-<ImageId>.mat
 * sFileNameShort:	Materials\dat-<GroupId>.mat
 *
 * Both sFileName and sFileNameShort must have iFileNameSize bytes to store the string.
 * sFileNameShort can be used to load mat files for animations, like explosions.
 */
void DATNameToMATParamsFile(char *DATName, char *sFileName, char *sFileNameShort, int iFileNameSize) {
	int GroupId, ImageId;
	// Get the GroupId and ImageId from the DATName, nullify sFileName if we can't extract
	// the data from the name
	if (!GetGroupIdImageIdFromDATName(DATName, &GroupId, &ImageId)) {
		sFileName[0] = 0;
		return;
	}
	// Build the material filename for the DAT texture:
	snprintf(sFileName, iFileNameSize, "Materials\\dat-%d-%d.mat", GroupId, ImageId);
	snprintf(sFileNameShort, iFileNameSize, "Materials\\dat-%d.mat", GroupId);
}


/*
void InitCachedMaterials() {
	for (int i = 0; i < MAX_CACHED_MATERIALS; i++)
		g_CachedMaterials[i].texname[0] = 0;
	g_iFirstCachedMaterial = g_iLastCachedMaterial = 0;
}
*/

void InitOPTnames() {
	ClearOPTnames();
}

void ClearOPTnames() {
	g_OPTnames.clear();
}

void InitCraftMaterials() {
	ClearCraftMaterials();
	//log_debug("[DBG] [MAT] g_Materials initialized (cleared)");
}

void ClearCraftMaterials() {
	for (uint32_t i = 0; i < g_Materials.size(); i++) {
		// Release the materials for each craft
		g_Materials[i].MaterialList.clear();
	}
	// Release the global materials
	g_Materials.clear();
	//log_debug("[DBG] [MAT] g_Materials cleared");
}

/*
Find the index where the materials for the specific OPT is loaded, or return -1 if the
OPT isn't loaded yet.
*/
int FindCraftMaterial(char* OPTname) {
	for (uint32_t i = 0; i < g_Materials.size(); i++) {
		if (_stricmp(OPTname, g_Materials[i].OPTname) == 0)
			return i;
	}
	return -1;
}

/*
Find the material in the specified CraftIndex of g_Materials that corresponds to
TexName. Returns the default material if it wasn't found or if TexName is null/empty
*/
Material FindMaterial(int CraftIndex, char* TexName, bool debug) {
	CraftMaterials* craftMats = &(g_Materials[CraftIndex]);
	// Slot should always be present and it should be the default craft material
	Material defMat = craftMats->MaterialList[0].material;
	if (TexName == NULL || TexName[0] == 0)
		return defMat;
	for (uint32_t i = 1; i < craftMats->MaterialList.size(); i++) {
		if (_stricmp(TexName, craftMats->MaterialList[i].texname) == 0) {
			defMat = craftMats->MaterialList[i].material;
			if (debug)
				log_debug("[DBG] [MAT] Material %s found: M:%0.3f, I:%0.3f, G:%0.3f",
					TexName, defMat.Metallic, defMat.Intensity, defMat.Glossiness);
			return defMat;
		}
	}

	if (debug)
		log_debug("[DBG] [MAT] Material %s not found, returning default: M:%0.3f, I:%0.3f, G:%0.3f",
			TexName, defMat.Metallic, defMat.Intensity, defMat.Glossiness);
	return defMat;
}

void AnimatedTexControl::ResetAnimation() {
	this->AnimIdx = 0;
	if (this->Sequence.size() >= 1)
		this->TimeLeft = this->Sequence[0].seconds;
}

void AnimatedTexControl::Animate() {
	int num_frames = this->Sequence.size();
	if (num_frames == 0) return;

	int idx = this->AnimIdx;
	float time = this->TimeLeft - g_HiResTimer.elapsed_s;
	// If the 3D display is paused for an extended period of time (for instance, if
	// the user presses ESC to display the menu for several seconds), then the animation
	// will have to advance for several seconds as well, so we need the "while" below
	// to advance the animation accordingly.
	while (time < 0.0f) {
		if (this->IsRandom)
			idx = rand() % num_frames;
		else {
			if (NoLoop)
				idx = min(num_frames - 1, idx + 1);
			else
				idx = (idx + 1) % num_frames;
		}
		// Use the time specified in the sequence for the next index
		if (this->Sequence[idx].seconds < 0.00001f) { // prevent infinite loops
			time = 0.0f;
		} else
			time += this->Sequence[idx].seconds;
	}
	this->AnimIdx = idx;
	this->TimeLeft = time;
}

void AnimateMaterials() {
	// I can't use a shorthand loop like the following:
	// for (AnimatedTexControl atc : g_AnimatedMaterials) 
	// because that'll create local copies of each element in the std::vector in atc.
	// In other words, the iterator is a local copy of each element in g_AnimatedMaterials,
	// which doesn't work, because we need to modify the source element
	for (uint32_t i = 0; i < g_AnimatedMaterials.size(); i++) {
		AnimatedTexControl *atc = &(g_AnimatedMaterials[i]);
		// Reset this ATC if its event fired during the current frame
		if (EventFired(atc->Event))
			atc->ResetAnimation();
		else
			atc->Animate();
	}
}

void ClearAnimatedMaterials() {
	for (AnimatedTexControl atc : g_AnimatedMaterials)
		atc.Sequence.clear();
	g_AnimatedMaterials.clear();
}

void CockpitInstrumentState::FromXWADamage(WORD XWADamage) {
	CockpitDamage x;
	x.Flags = XWADamage;
	CMD				= (x.CMD != 0);
	LaserIon		= (x.LaserIon == 0x7);
	BeamWeapon		= (x.BeamWeapon != 0);
	Shields			= (x.Shields != 0);
	Throttle		= (x.Throttle != 0);
	Sensors			= (x.Sensors == 0x3);
	LaserRecharge	= (x.LaserRecharge != 0);
	EnginePower		= (x.EngineLevel != 0);
	ShieldRecharge	= (x.ShieldRecharge  != 0);
	BeamRecharge	= (x.BeamRecharge != 0);
}

void ResetGameEvent() {
	g_GameEvent.TargetEvent = EVT_NONE;
	memset(&(g_GameEvent.CockpitInstruments), 1, sizeof(CockpitInstrumentState));
	g_GameEvent.HullEvent = EVT_NONE;

	// Add new events here
	// ...

	// Reset the Events Fired flags
	for (int i = 0; i < MAX_GAME_EVT; i++)
		bEventsFired[i] = false;

	g_PrevGameEvent = g_GameEvent;
}

// Check which events were set on the current frame and set the corresponding flags
void UpdateEventsFired() {
	// Set all events to false
	for (int i = 0; i < MAX_GAME_EVT; i++)
		bEventsFired[i] = false;
	
	// Set the current target event to true if it changed:
	if (g_PrevGameEvent.TargetEvent != g_GameEvent.TargetEvent)
		bEventsFired[g_GameEvent.TargetEvent] = true;

	// Set the current hull damage event to true if it changed:
	if (g_PrevGameEvent.HullEvent != g_GameEvent.HullEvent)
		bEventsFired[g_GameEvent.HullEvent] = true;
	
	// Manually check and set each damage event
	bEventsFired[CPT_EVT_BROKEN_CMD] = (g_PrevGameEvent.CockpitInstruments.CMD && !g_GameEvent.CockpitInstruments.CMD);
	bEventsFired[CPT_EVT_BROKEN_LASER_ION] = (g_PrevGameEvent.CockpitInstruments.LaserIon && !g_GameEvent.CockpitInstruments.LaserIon);
	bEventsFired[CPT_EVT_BROKEN_BEAM_WEAPON] = (g_PrevGameEvent.CockpitInstruments.BeamWeapon && !g_GameEvent.CockpitInstruments.BeamWeapon);

	bEventsFired[CPT_EVT_BROKEN_SHIELDS] = (g_PrevGameEvent.CockpitInstruments.Shields && !g_GameEvent.CockpitInstruments.Shields);
	bEventsFired[CPT_EVT_BROKEN_THROTTLE] = (g_PrevGameEvent.CockpitInstruments.Throttle && !g_GameEvent.CockpitInstruments.Throttle);
	bEventsFired[CPT_EVT_BROKEN_SENSORS] = (g_PrevGameEvent.CockpitInstruments.Sensors && !g_GameEvent.CockpitInstruments.Sensors);

	bEventsFired[CPT_EVT_BROKEN_LASER_RECHARGE] = (g_PrevGameEvent.CockpitInstruments.LaserRecharge && !g_GameEvent.CockpitInstruments.LaserRecharge);
	bEventsFired[CPT_EVT_BROKEN_ENGINE_POWER] = (g_PrevGameEvent.CockpitInstruments.EnginePower && !g_GameEvent.CockpitInstruments.EnginePower);
	bEventsFired[CPT_EVT_BROKEN_SHIELD_RECHARGE] = (g_PrevGameEvent.CockpitInstruments.ShieldRecharge && !g_GameEvent.CockpitInstruments.ShieldRecharge);
	bEventsFired[CPT_EVT_BROKEN_BEAM_RECHARGE] = (g_PrevGameEvent.CockpitInstruments.BeamRecharge && !g_GameEvent.CockpitInstruments.BeamRecharge);

	for (int i = 0; i < MAX_GAME_EVT; i++)
		if (bEventsFired[i]) log_debug("[DBG] --> Event [%s] FIRED", g_sGameEventNames[i]);

	// Copy the events
	g_PrevGameEvent = g_GameEvent;
}

bool EventFired(GameEvent Event) {
	return bEventsFired[Event];
}
