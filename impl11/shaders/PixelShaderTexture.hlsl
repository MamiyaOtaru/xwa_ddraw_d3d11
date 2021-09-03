// Copyright (c) 2014 J�r�my Ansel
// Licensed under the MIT license. See LICENSE.txt
// Extended for VR by Leo Reyes (c) 2019
#include "shader_common.h"
#include "HSV.h"
#include "shading_system.h"
#include "PixelShaderTextureCommon.h"

Texture2D    texture0 : register(t0);
SamplerState sampler0 : register(s0);

// Texture slot 9 (and above) seem to be free. We might be able to use other slots, but I don't
// want to break something by doing that. Will have to come back later and check if it's possible
// to save some slots
Texture2D    greebleTex0 : register(t9);
SamplerState greebleSamp0 : register(s9);

// pos3D/Depth buffer has the following coords:
// X+: Right
// Y+: Up
// Z+: Away from the camera
// (0,0,0) is the camera center, (0,0,Z) is the center of the screen

//#define diffuse_intensity 0.95

//static float3 light_dir = float3(0.9, 1.0, -0.8);
//#define ambient 0.03
//static float3 ambient_col = float3(0.025, 0.025, 0.03);
//static float3 ambient_col = float3(0.10, 0.10, 0.15);

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

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	float4 texelColor = texture0.Sample(sampler0, input.tex);
	float  alpha	  = texelColor.w;
	float3 diffuse    = lerp(input.color.xyz, 1.0, fDisableDiffuse);
	float3 P		  = input.pos3D.xyz;
	float  SSAOAlpha  = saturate(min(alpha - fSSAOAlphaOfs, fPosNormalAlpha));
	// Zero-out the bloom mask.
	output.bloom  = 0;
	output.color  = texelColor;
	output.pos3D  = float4(P, SSAOAlpha);
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
	// N *= input.normal.w; // Zero-out normals when w == 0 ?
	
	//if (N.z < 0.0) N.z = 0.0; // Avoid vectors pointing away from the view
	// Flipping N.z seems to have a bad effect on SSAO: flat unoccluded surfaces become shaded
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

	// DEBUG
	//output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
	//return output;
	// DEBUG

	if (special_control == SPECIAL_CONTROL_SMOKE)
	{
		//output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
		const float a   = 0.1 * alpha;
		output.color    = float4(texelColor.rgb, a);
		output.ssaoMask = float4(fSSAOMaskVal, fGlossiness, fSpecInt, a);
		output.ssMask   = float4(fNMIntensity, fSpecVal, 0.0, a);
		return output;
	}

	

	/*
	if (special_control == SPECIAL_CONTROL_EXPLOSION)
	{
		output.color = float4(0, 1, 0, alpha);
		output.bloom = output.color;
		return output;
	}
	*/

	// Process lasers (make them brighter in 32-bit mode)
	if (bIsLaser) {
		output.pos3D.a = 0;
		output.normal.a = 0;
		//output.ssaoMask.a = 1; // Needed to write the emission material for lasers
		output.ssaoMask.a = 0; // We should let the regular material properties on lasers so that they become emitters
		output.ssMask.a = 0;
		//output.diffuse = 0;
		// This is a laser texture, process the bloom mask accordingly
		float3 HSV = RGBtoHSV(texelColor.xyz);
		HSV.y *= 1.5;
		if (bIsLaser > 1) {
			// Enhance the lasers in 32-bit mode
			// Increase the saturation and lightness
			HSV.z *= 2.0;
			float3 color = HSVtoRGB(HSV);
			output.color = float4(color, alpha);
			output.bloom = float4(color, alpha);
		}
		else {
			output.color = texelColor; // Return the original color when 32-bit mode is off
			// Enhance the saturation for lasers
			float3 color = HSVtoRGB(HSV);
			output.bloom = float4(color, alpha);
		}
		output.bloom.rgb *= fBloomStrength;
		return output;
	}

	// Process light textures (make them brighter in 32-bit mode)
	if (bIsLightTexture) {
		output.normal.a = 0;
		//output.ssaoMask.r = SHADELESS_MAT;
		output.ssMask = 0; // Normal Mapping intensity --> 0
		//output.pos3D = 0;
		//output.normal = 0;
		//output.diffuse = 0;
		float3 color = texelColor.rgb;
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
		return output;
	}

	// Enhance the engine glow. In this texture, the diffuse component also provides
	// the hue. The engine glow is also used to render smoke, so that's why the smoke
	// glows.
	if (bIsEngineGlow) {
		// Disable depth-buffer write for engine glow textures
		output.pos3D.a = 0;
		output.normal.a = 0;
		output.ssaoMask.a = 0;
		output.ssMask.a = 0;
		float3 color = texelColor.rgb * input.color.xyz;
		// This is an engine glow, process the bloom mask accordingly
		if (bIsEngineGlow > 1) {
			// Enhance the glow in 32-bit mode
			float3 HSV = RGBtoHSV(color);
			//HSV.y *= 1.15;
			HSV.y *= 1.25;
			HSV.z *= 1.25;
			color = HSVtoRGB(HSV);
		} 
		output.color = float4(color, alpha);
		output.bloom = float4(fBloomStrength * output.color.rgb, alpha);
		return output;
	}

	// The HUD is shadeless and has transparency. Some planets in the background are also 
	// transparent (CHECK IF Jeremy's latest hooks fixed this) 
	// So glass is a non-shadeless surface with transparency:
	if (fSSAOMaskVal < SHADELESS_LO && !bIsShadeless && alpha < 0.95) {
		// Change the material and do max glossiness and spec_intensity
		output.ssaoMask.r = GLASS_MAT;
		output.ssaoMask.gba = 1.0;
		// Also write the normals of this surface over the current background
		output.normal.a = 1.0;
		output.ssMask.r = 0.0; // No normal mapping
		output.ssMask.g = 1.0; // White specular value
		output.ssMask.a = 1.0; // Make glass "solid" in the mask texture
	}

	// Original code:
	output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
	
	if (special_control == SPECIAL_CONTROL_ADD_GREEBLE)
	{
		//float4 greeble = float4(1, 0, 0, output.color.a);
		//float greeble_factor = 0.75;
		const float greeble_scale = 1.5;
		const float greeble_mix = 0.9;
		// Sample the greeble texture
		float4 greeble = greebleTex0.Sample(greebleSamp0, frac(greeble_scale * input.tex));
		// Mix the greeble with the current texture, use either the overlay or multiply blending modes
		//float4 greebleMix = lerp(output.color, overlay(output.color, greeble), greeble_mix);
		float4 greebleMix = lerp(output.color, output.color * greeble * greeble, greeble_mix);
		// Display the greeble mix depending on the depth of the current point.
		output.color = lerp(greebleMix, output.color, saturate(P.z / 500.0));
		return output;
	}

	//if (special_control == SPECIAL_CONTROL_BACKGROUND)
	//	output.color.r += 0.7;
	return output;
}
