// Copyright (c) 2014 J�r�my Ansel
// Licensed under the MIT license. See LICENSE.txt
// Extended for VR by Leo Reyes (c) 2019
#include "shader_common.h"
#include "HSV.h"
#include "shading_system.h"
#include "PixelShaderTextureCommon.h"

Texture2D    texture0 : register(t0);
SamplerState sampler0 : register(s0);

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


// ****************************************
// I should remove this later...
#include "ShaderToyDefs.h"

#define SPEED 0.25
#define PERIOD 4.0 // Increase this to make more/thinner rays

// 3D noise from: https://www.shadertoy.com/view/4sfGzS
float hash(vec3 p)
{
	p = fract(p*0.3183099 + .1);
	p *= 17.0;
	return fract(p.x*p.y*p.z*(p.x + p.y + p.z));
}

float noise(in vec3 x)
{
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f*(3.0 - 2.0*f);

	return mix(mix(mix(hash(i + vec3(0, 0, 0)),
		hash(i + vec3(1, 0, 0)), f.x),
		mix(hash(i + vec3(0, 1, 0)),
			hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash(i + vec3(0, 0, 1)),
			hash(i + vec3(1, 0, 1)), f.x),
			mix(hash(i + vec3(0, 1, 1)),
				hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

static float3x3 m = float3x3(
	 0.00,  0.80,  0.60,
	-0.80,  0.36, -0.48,
	-0.60, -0.48,  0.64
);

float fbm(vec3 q) {
	float f;
	f = 0.5000*noise(q);  q = mul(m, q*2.01);
	f += 0.2500*noise(q); q = mul(m, q*2.02);
	f += 0.1250*noise(q); q = mul(m, q*2.03);
	f += 0.0625*noise(q); q = mul(m, q*2.01);
	return f;
}
// ****************************************

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
	// SS Mask: Normal Mapping Intensity, Specular Value, unused
	output.ssMask = float4(fNMIntensity, fSpecVal, 0.0, alpha);

	if (bIsSun)
	{
		// Disable depth-buffer write, etc. for sun textures
		output.pos3D = 0;
		output.normal = 0;
		output.ssaoMask = 0;
		output.ssMask = 0;

		// SunColor.a selects either white (0) or a light color (1) specified by SunColor.rgb:
		float3 corona_color = lerp(1.0, SunColor.rgb, SunColor.a);
		float2 v = float2(input.tex.xy - 0.5);
		const float V_2 = dot(v, v);
		float intensity = saturate(pow(0.01 / V_2, 1.8));
		//const float3 light_color = float3(0.3, 0.3, 1.0);
		//const float3 light_color = 1.0;
		// Center disk:
		output.color = intensity;
		output.bloom = float4(fBloomStrength * output.color.xyz * corona_color, intensity);

		// Add the corona
		float2 pos = 8.0 * v;
		float r = length(pos);
		v = pos / r;
		r -= 1.0;

		//float f = fbm(vec3(PERIOD * p, SPEED * t - 0.5 * r));
		// This version shows straight lines:
		//float f = fbm(vec3(PERIOD * p, SPEED * t));
		float f = fbm(vec3(PERIOD * v, SPEED * iTime - 0.25 * r));
		// Fade out the noise:
		f = clamp(f + 1.5 * exp(-0.5 * r) - 0.8, 0.0, 1.0);
		float cursor = f * 0.75;
		cursor *= cursor * cursor;
		corona_color = mix(corona_color, 1.0, cursor);
		vec3 color = 0.9 * f * corona_color;

		//r = clamp(r * 6.0, 0.0, 1.0);
		//col += r * clamp(color, 0.0, 1.0);
		intensity = exp(-r * 0.001) * 0.9 * f;
		output.color.rgb += exp(-r * 0.001) * clamp(color, 0.0, 1.0);
		output.color.a += intensity;
		//output.color.xyz *= output.color.xyz; // Gamma compensation?
		return output;
	}

	// DEBUG
	//output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
	//return output;
	// DEBUG

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
	// the hue
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

	// hook_normals code:
	/*
	if (input.normal.w > 0.0) {
		// DEBUG
		//output.color.xyz = input.normal.xyz;
		// X+ is to the right: the light comes from the right
		//output.color.xyz = input.normal.xxx;
		// Y+ is down: the light comes from below
		//output.color.xyz = input.normal.yyy;
		// Z+ is away from the camera: the light comes from far away (behind the objects in the tech room)
		//output.color.xyz = input.normal.zzz;
		//output.color.w = alpha;
		//return output;
		// DEBUG

		float3 L = normalize(light_dir);

		// Gamma
		texelColor.xyz = pow(clamp(texelColor.xyz, 0.0, 1.0), 2.2);

		// diffuse component
		float diffuse = clamp(dot(N, L), 0.0, 1.0);
		diffuse *= diffuse_intensity;
		// specular component
		float3 eye = float3(0.0, 0.0, 0.0);
		//float3 spec_col = texelColor.xyz;
		float3 spec_col = clamp(1.5 * texelColor.xyz, 0.0, 1.0);
		//float3 spec_col = 0.35;
		float3 eye_vec  = normalize(eye - P);
		float3 refl_vec = normalize(reflect(-L, N));
		float  spec     = clamp(dot(eye_vec, refl_vec), 0.0, 1.0);
		float  exponent = 10.0;
		if (alpha < 0.95) { // Transparent polygons --> glass
			exponent = 128.0;
			spec_col = 1.0;
		}
		spec = pow(spec, exponent);
		if (alpha < 0.95) alpha += 2.0 * spec; // Make specular reflections on glass more visible
		
		//output.color = float4((ambient_col + diffuse) * texelColor.xyz + spec_col * spec, texelColor.w);
		//output.color = float4((ambient_col + diffuse) * texelColor.xyz, texelColor.w);
		output.color = float4((ambient + diffuse) * texelColor.xyz + spec * spec_col, alpha);
		//output.color.xyz = N * 0.5 + 0.5;

		// Gamma
		output.color.xyz = pow(clamp(output.color.xyz, 0.0, 1.0), 0.45);

		output.color.xyz *= brightness;
	} 
	else
	{
		// Objects without normals don't need to go gamma correction, we would exp(color, 2.2)
		// only to do exp(color, 0.45), so no need to do that as it will cancel itself out.
		output.color = float4(brightness * diffuse * texelColor.xyz, texelColor.w);
		//output.color = float4(input.tex, 0, texelColor.w);
	}
	*/

	return output;
}
