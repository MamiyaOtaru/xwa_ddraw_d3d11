// Copyright (c) 2021 Leo Reyes
// Licensed under the MIT license. See LICENSE.txt
#include "shader_common.h"
#include "HSV.h"
#include "shading_system.h"
#include "PixelShaderTextureCommon.h"
#include "ShaderToyDefs.h"

Texture2D    texture0 : register(t0);
SamplerState sampler0 : register(s0);

// Texture slot 9 (and above) seem to be free. We might be able to use other slots, but I don't
// want to break something by doing that. I'll check that later

// Greeble Mask, slot 9
Texture2D    greebleMaskTex : register(t9);
SamplerState greebleMaskSamp : register(s9);

// Greeble Texture 1, slot 10
Texture2D    greebleTex1 : register(t10);
SamplerState greebleSamp1 : register(s10);

// Greeble Texture 2, slot 11
Texture2D    greebleTex2 : register(t11);
SamplerState greebleSamp2 : register(s11);

// pos3D/Depth buffer has the following coords:
// X+: Right
// Y+: Up
// Z+: Away from the camera
// (0,0,0) is the camera center, (0,0,Z) is the center of the screen
// The Normal buffer is different:
// Z+ points towards the camera.

struct PixelShaderInput
{
	float4 pos    : SV_POSITION;
	float4 color  : COLOR0;
	float2 tex    : TEXCOORD0;
	float4 pos3D  : COLOR1;
	float4 normal : NORMAL;
};

struct PixelShaderOutput
{
	float4 color    : SV_TARGET0;
	float4 bloom    : SV_TARGET1;
	float4 pos3D    : SV_TARGET2;
	float4 normal   : SV_TARGET3;
	float4 ssaoMask : SV_TARGET4;
	float4 ssMask   : SV_TARGET5;
};

inline float4 overlay(float4 a, float4 b)
{
	return (a < 0.5) ? 2.0 * a * b : 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
}

inline float4 screen(float4 a, float4 b)
{
	return 1.0 - (1.0 - a) * (1.0 - b);
}

// From: http://www.thetenthplanet.de/archives/1180
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
	/*
	I made a little mess when building the normal and depth buffers.

	The depth buffer's coord sys looks like this:
		X+: Right
		Y+: Up
		Z+: Away from the camera

	But the normal buffer looks like this:
		X+: Right
		Y+: Up
		Z+: Pointing directly into the camera

	These systems are inconsistent: the Z coordinate is flipped between these two systems. According
	to the structure of the depth buffer, Z+ should point away from the camera, but in the normal buffer Z+
	points into the camera.

	I did this on purpose, I flipped the Z coordinate of the normals so that blue meant the surface was pointing
	into the camera (I also flipped the Y coordinate, but that's irrelevant because Y is consistent with the
	depth buffer). This made it easier to debug the normal buffer because it looks exactly like a regular normal
	map.

	Now, as a consequence, I have to flip the Z coordinate of the normals in the computations below, so that they
	are consistent with the depth buffer and the 3D position (p). Then at the end of all the math, I have to flip
	Z again so that it's consistent with the rest of the code that expects Z+ to point into the camera. Z is
	flipped again at the end of perturb_normal().
	*/
	N.z = -N.z;
	// get edge vectors of the pixel triangle
	vec3 dp1 = ddx(p);
	vec3 dp2 = ddy(p);
	vec2 duv1 = ddx(uv);
	vec2 duv2 = ddy(uv);

	// solve the linear system
	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame
	float invmax = rsqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

vec3 perturb_normal(vec3 N, vec3 V, vec2 texcoord, vec3 NMCol) {
	// N: the interpolated vertex normal and
	// V: the view vector (vertex to eye)
	// NMCol: the raw sampled color from the blue-ish normal map.
	NMCol.rgb = normalize((NMCol.rgb - 0.5) * 2.0);
	mat3 TBN = cotangent_frame(N, -V, texcoord); // V is the view vector, so -V is equivalent to the vertex 3D pos
	//return normalize(mul(NMCol, TBN));
	// We flip the Z coordinate for... reasons. See cotangent_frame() for the explanation.
	// I _think_ mul(vec3, mat3) is the right order because of the way the matrix was built.
	vec3 tempN = normalize(mul(NMCol, TBN));
	tempN.z = -tempN.z;
	return tempN;
}

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	float4 texelColor = texture0.Sample(sampler0, input.tex);
	float  alpha = texelColor.w;
	float3 diffuse = lerp(input.color.xyz, 1.0, fDisableDiffuse);
	float3 P = input.pos3D.xyz;
	float  SSAOAlpha = saturate(min(alpha - fSSAOAlphaOfs, fPosNormalAlpha));
	// Zero-out the bloom mask.
	output.bloom = 0;
	output.color = texelColor;
	output.pos3D = float4(P, SSAOAlpha);
	output.ssMask = 0;

	// DEBUG
	//output.normal = 0;
	//output.ssaoMask = 0;
	//output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
	//return output;
	// DEBUG

	// DEBUG
		//output.color = float4(frac(input.tex.xy), 0, 1); // DEBUG: Display the uvs as colors
		//output.ssaoMask = float4(SHADELESS_MAT, 0, 0, 1);
		//output.ssMask = 0;
		//output.normal = 0;
		//return output;
	// DEBUG

	// Original code:
	//float3 N = normalize(cross(ddx(P), ddy(P)));
	// Since Z increases away from the camera, the normals end up being negative when facing the
	// viewer, so let's mirror the Z axis:
	// Test:
	//N.z = -N.z;

	// hook_normals code:
	float3 N = normalize(input.normal.xyz * 2.0 - 1.0);
	N.y = -N.y; // Invert the Y axis, originally Y+ is down
	N.z = -N.z;

	output.normal = float4(N, SSAOAlpha);

	// SSAO Mask/Material, Glossiness, Spec_Intensity
	// Glossiness is multiplied by 128 to compute the exponent
	//output.ssaoMask = float4(fSSAOMaskVal, DEFAULT_GLOSSINESS, DEFAULT_SPEC_INT, alpha);
	// ssaoMask.r: Material
	// ssaoMask.g: Glossiness
	// ssaoMask.b: Specular Intensity
	output.ssaoMask = float4(fSSAOMaskVal, fGlossiness, fSpecInt, alpha);
	// SS Mask: Normal Mapping Intensity, Specular Value, Shadeless
	output.ssMask = float4(fNMIntensity, fSpecVal, fAmbient, alpha);

	// We shouldn't call this shader for smoke
	//if (special_control == SPECIAL_CONTROL_SMOKE)

	// Never call this shader for lasers
	//if (bIsLaser) {
	
	

	// Enhance the engine glow.
	// Never call this shader for engine glow
	
	// Never call this shader for the following:
	// The HUD is shadeless and has transparency. Some planets in the background are also 
	// transparent (CHECK IF Jeremy's latest hooks fixed this) 
	// So glass is a non-shadeless surface with transparency:
	//if (fSSAOMaskVal < SHADELESS_LO && !bIsShadeless && alpha < 0.95) {
	
	// Original code:
	output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);

	/*
	uint GreebleControl;		// Bitmask: 0x100 -- Use Greeble Mask
								// 0x003 First Tex Blend Mode
								// 0x00C Second Tex Blend Mode
								// 0x001: Multiply
								// 0x002: Overlay
	// GBM_MULTIPLY = 1,
	// GBM_OVERLAY = 2,
	// GBM_SCREEN = 3,
	// GBM_REPLACE = 4,
	// GBM_NORMAL_MAP = 5
	*/

	uint BlendingMode1 = GreebleControl & 0x7;
	uint BlendingMode2 = (GreebleControl >> 3) & 0x7;
	bool bHasMask = (GreebleControl & 0x100) != 0x0;
	float4 maskCol = 1.0;
	float mask = 1.0;

	// Sample the mask, we should use the UVs of the original texture. The mask should be defined
	// with respect to the original texture. The mask is expected to be a grayscale image, but we
	// also multiply with the alpha component so that either black or alpha can inhibit greebles.
	if (bHasMask) {
		maskCol = greebleMaskTex.Sample(greebleMaskSamp, input.tex);
		mask = maskCol.a * dot(0.333, maskCol.rgb);
	}

	// Sample the greeble textures. If this shader is called, it should at least have one
	// greeble texture activated.
	float4 greeble1 = greebleTex1.Sample(greebleSamp1, frac(GreebleScale1 * input.tex));
	float4 greeble2 = 0.0;
	float4 greebleMixCol = 0.0;
	// Convert grayscale into transparency for lightmaps
	if (bIsLightTexture) greeble1.a *= dot(0.333, greeble1.rgb);

	// Mix the greeble with the current texture, use either the overlay or multiply blending modes
	//greeble1 = float4(1, 0, 0, 1);
	if (BlendingMode1 == 1)
		greebleMixCol = lerp(output.color, output.color * greeble1, GreebleMix1 * greeble1.a * mask);
	else if (BlendingMode1 == 2)
		greebleMixCol = lerp(output.color, overlay(output.color, greeble1), GreebleMix1 * greeble1.a * mask);
	else if (BlendingMode1 == 3)
		greebleMixCol = lerp(output.color, screen(output.color, greeble1), GreebleMix1 * greeble1.a * mask);
	else if (BlendingMode1 == 4)
		// For the replace mode, we ignore greeble1.a because we want to replace the original texture everywhere,
		// including the transparent areas
		greebleMixCol = lerp(output.color, greeble1, GreebleMix1 * mask);
	else if (BlendingMode1 == 5) {
		output.normal.xyz = lerp(output.normal.xyz,
			perturb_normal(output.normal.xyz, /* g_viewvector */ -P, input.tex, greeble1.rgb),
			GreebleMix1 * greeble1.a * mask);
		greebleMixCol = output.color;
	}
	// Mix the greeble color depending on the current depth
	output.color = lerp(greebleMixCol, output.color, saturate(P.z / GreebleDist1));

	if (BlendingMode2 != 0) {
		greeble2 = greebleTex2.Sample(greebleSamp2, frac(GreebleScale2 * input.tex));
		// Convert grayscale into transparency for lightmaps
		if (bIsLightTexture) greeble2.a *= dot(0.333, greeble2.rgb);
		//greeble2 = float4(0, 1, 0, 1);
		if (BlendingMode2 == 1)
			greebleMixCol = lerp(output.color, output.color * greeble2, GreebleMix2 * greeble2.a * mask);
		else if (BlendingMode2 == 2)
			greebleMixCol = lerp(output.color, overlay(output.color, greeble2), GreebleMix2 * greeble2.a * mask);
		else if (BlendingMode2 == 3)
			greebleMixCol = lerp(output.color, screen(output.color, greeble2), GreebleMix2 * greeble2.a * mask);
		else if (BlendingMode2 == 4)
			// For the replace mode, we ignore greeble2.a because we want to replace the original texture everywhere,
			// including the transparent areas
			greebleMixCol = lerp(output.color, greeble2, GreebleMix2 * mask);
		else if (BlendingMode2 == 5) {
			output.normal.xyz = lerp(output.normal.xyz,
				perturb_normal(output.normal.xyz, /* g_viewvector */ -P, input.tex, greeble2.rgb),
				GreebleMix2 * greeble2.a * mask);
			greebleMixCol = output.color;
		}
		// Mix the greeble color depending on the current depth
		output.color = lerp(greebleMixCol, output.color, saturate(P.z / GreebleDist2));
	}

	// Process light textures (make them brighter in 32-bit mode)
	if (bIsLightTexture) {
		output.normal.a = 0;
		//output.ssaoMask.r = SHADELESS_MAT;
		output.ssMask = 0; // Normal Mapping intensity --> 0
		//output.pos3D = 0;
		//output.normal = 0;
		//output.diffuse = 0;
		//float3 color = texelColor.rgb;
		float3 color = output.color.rgb; // <-- This makes this section different from the lightmap shader in PixelShaderTexture
		alpha = output.color.a;
		// This is a light texture, process the bloom mask accordingly
		float3 HSV = RGBtoHSV(color);
		float val = HSV.z;
		// Enhance = true
		if (bIsLightTexture > 1) {
			// Make the light textures brighter in 32-bit mode
			HSV.z *= 1.25;
			//val *= 1.25; // Not sure if it's a good idea to increase val here
			// It's not! It'll make a few OPTs bloom when they didn't in 1.1.1
			// The alpha for light textures is either 0 or >0.1, so we multiply by 10 to
			// make it [0, 1]
			alpha *= 10.0;
			color = HSVtoRGB(HSV);
		}
		//if (val > 0.8 && alpha > 0.5) {
			// We can't do smoothstep(0.0, 0.8, val) because the hangar will bloom all over the
			// place. Many other textures may have similar problems too.
		const float bloom_alpha = smoothstep(0.75, 0.85, val) * smoothstep(0.45, 0.55, alpha);
		//const float bloom_alpha = smoothstep(0.75, 0.81, val) * smoothstep(0.45, 0.51, alpha);
		output.bloom = float4(bloom_alpha * val * color, bloom_alpha);
		output.ssaoMask.ra = bloom_alpha;
		output.ssMask.a = bloom_alpha;
		//}
		output.color = float4(color, alpha);
		output.bloom.rgb *= fBloomStrength;
		if (bInHyperspace && output.bloom.a < 0.5) {
			//output.color.a = 1.0;
			discard; // VERIFIED: Works fine in Win7
		}
		//return output;
	}

	return output;
}
