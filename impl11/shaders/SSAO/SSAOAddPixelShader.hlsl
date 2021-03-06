/*
 * Simple shader to add SSAO + Bloom-Mask + Color Buffer
 * Copyright 2019, Leo Reyes.
 * Licensed under the MIT license. See LICENSE.txt
 *
 * The bloom-mask is *not* the final bloom buffer -- this mask is output by the pixel
 * shader and it will be used later to compute proper bloom. Here we use this mask to
 * disable areas of the SSAO buffer that should be bright.
 */
#include "..\shader_common.h"
#include "..\HSV.h"
#include "..\shading_system.h"
#include "..\SSAOPSConstantBuffer.h"

// The color buffer
Texture2D texColor : register(t0);
SamplerState sampColor : register(s0);

// The SSAO buffer
Texture2D texSSAO : register(t1);
SamplerState samplerSSAO : register(s1);

// The SSAO mask
Texture2D texSSAOMask : register(t2);
SamplerState samplerSSAOMask : register(s2);

// The Normals buffer
Texture2D texNormal : register(t3);
SamplerState samplerNormal : register(s3);

// The Foreground 3D position buffer (linear X,Y,Z)
Texture2D    texPos   : register(t4);
SamplerState sampPos  : register(s4);

// The Background 3D position buffer (linear X,Y,Z)
Texture2D    texPos2  : register(t5);
SamplerState sampPos2 : register(s5);

// The Shading System Mask buffer
Texture2D texSSMask : register(t6);
SamplerState samplerSSMask : register(s6);

// We're reusing the same constant buffer used to blur bloom; but here
// we really only use the amplifyFactor to upscale the SSAO buffer (if
// it was rendered at half the resolution, for instance)
cbuffer ConstantBuffer : register(b2)
{
	float pixelSizeX, pixelSizeY, white_point, amplifyFactor;
	// 16 bytes
	float bloomStrength, uvStepSize, saturationStrength;
	uint enableSSAO;
	// 32 bytes
	uint enableBentNormals;
	float norm_weight, unused2, unused3;
};

/*
// SSAOPixelShaderCBuffer
cbuffer ConstantBuffer : register(b3)
{
	float screenSizeX, screenSizeY, indirect_intensity, bias;
	// 16 bytes
	float intensity, near_sample_radius, black_level;
	uint samples;
	// 32 bytes
	uint z_division;
	float bentNormalInit, max_dist, power;
	// 48 bytes
	uint ssao_debug;
	float moire_offset, ssao_amplifyFactor;
	uint fn_enable;
	// 64 bytes
	float fn_max_xymult, fn_scale, fn_sharpness, nm_intensity_near;
	// 80 bytes
	float far_sample_radius, nm_intensity_far, ambient, ssao_amplifyFactor2;
	// 96 bytes
	float x0, y0, x1, y1; // Viewport limits in uv space
	// 112 bytes
	float3 invLightColor;
	float gamma;
	// 128 bytes
	float white_point_ssdo, shadow_step_size, shadow_steps, aspect_ratio;
	// 144 bytes
	float4 vpScale;
	// 160 bytes
	uint shadow_enable;
	float shadow_k, ssao_unused0, ssao_unused1;
	// 176 bytes
};
*/

cbuffer ConstantBuffer : register(b4)
{
	matrix projEyeMatrix;
	matrix viewMatrix;
	matrix fullViewMatrix;
};

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

struct PixelShaderOutput
{
	float4 color : SV_TARGET0;
	float4 bloom : SV_TARGET1;
};

float3 getPositionFG(in float2 uv, in float level) {
	// The use of SampleLevel fixes the following error:
	// warning X3595: gradient instruction used in a loop with varying iteration
	// This happens because the texture is sampled within an if statement (if FGFlag then...)
	return texPos.SampleLevel(sampPos, uv, level).xyz;
}

float3 getPositionBG(in float2 uv, in float level) {
	return texPos2.SampleLevel(sampPos2, uv, level).xyz;
}

/*
 * From Pascal Gilcher's SSR shader.
 * https://github.com/martymcmodding/qUINT/blob/master/Shaders/qUINT_ssr.fx
 * (Used with permission from the author)
 */
float3 get_normal_from_color(float2 uv, float2 offset, float nm_intensity)
{
	float3 offset_swiz = float3(offset.xy, 0);
	float nm_scale = fn_scale * nm_intensity;
	// Luminosity samples
	float hpx = dot(texColor.SampleLevel(sampColor, float2(uv + offset_swiz.xz), 0).xyz, 0.333) * nm_scale;
	float hmx = dot(texColor.SampleLevel(sampColor, float2(uv - offset_swiz.xz), 0).xyz, 0.333) * nm_scale;
	float hpy = dot(texColor.SampleLevel(sampColor, float2(uv + offset_swiz.zy), 0).xyz, 0.333) * nm_scale;
	float hmy = dot(texColor.SampleLevel(sampColor, float2(uv - offset_swiz.zy), 0).xyz, 0.333) * nm_scale;

	// Depth samples
	float dpx = getPositionFG(uv + offset_swiz.xz, 0).z;
	float dmx = getPositionFG(uv - offset_swiz.xz, 0).z;
	float dpy = getPositionFG(uv + offset_swiz.zy, 0).z;
	float dmy = getPositionFG(uv - offset_swiz.zy, 0).z;

	// Depth differences in the x and y axes
	float2 xymult = float2(abs(dmx - dpx), abs(dmy - dpy)) * fn_sharpness;
	//xymult = saturate(1.0 - xymult);
	xymult = saturate(fn_max_xymult - xymult);

	float3 normal;
	normal.xy = float2(hmx - hpx, hmy - hpy) * xymult / offset.xy * 0.5;
	normal.z = 1.0;

	return normalize(normal);
}

// n1: base normal
// n2: detail normal
float3 blend_normals(float3 n1, float3 n2)
{
	// I got this from Pascal Gilcher; but there's more details here:
	// https://blog.selfshadow.com/publications/blending-in-detail/
	//return normalize(float3(n1.xy*n2.z + n2.xy*n1.z, n1.z*n2.z));

	// UDN:
	//return normalize(float3(n1.xy + n2.xy, n1.z));

	n1.z += 1.0;
	n2.xy = -n2.xy;
	return normalize(n1 * dot(n1, n2) - n1.z * n2);
}

PixelShaderOutput main(PixelShaderInput input)
{
	float2 input_uv_sub = input.uv * amplifyFactor;
	float3 color         = texColor.Sample(sampColor, input.uv).xyz;
	float4 Normal        = texNormal.Sample(samplerNormal, input.uv);
	float3 ssao          = texSSAO.Sample(samplerSSAO, input_uv_sub).rgb;
	float3 ssaoMask      = texSSAOMask.Sample(samplerSSAOMask, input.uv).xyz;
	float3 ssMask        = texSSMask.Sample(samplerSSMask, input.uv).xyz;
	float  mask          = ssaoMask.x;
	float  gloss_mask    = ssaoMask.y;
	float  spec_int_mask = ssaoMask.z;
	float  diff_int      = 1.0;
	//bool   shadeless     = mask > GLASS_LO; // SHADELESS_LO;
	float  shadeless     = saturate((mask - GLASS_LO) / (GLASS_MAT - GLASS_LO)); // Avoid harsh transitions
	float  metallic      = mask / METAL_MAT;
	float  nm_int        = ssMask.x;
	float  spec_val      = ssMask.y;
	float3 pos3D;

	PixelShaderOutput output;
	output.color = 0;
	output.bloom = 0;

	// Normals with w == 0 are not available -- they correspond to things that don't have
	// normals, like the skybox
	//if (mask > 0.9 || Normal.w < 0.01) {
	if (Normal.w < 0.001) { // The skybox gets this alpha value
		output.color = float4(color, 1);
		return output;
	}
	// Avoid harsh transitions
	// The substraction below should be 1.0 - Normal.w; but I see alpha = 0.5 coming from the normals buf
	// because that gets written in PixelShaderTexture.hlsl in the alpha channel... I've got to check why
	// later. On the other hand, DC elements have alpha = 1.0 in their normals, so I've got to clamp too
	// or I'll get negative numbers
	shadeless = saturate(shadeless + saturate(2.0 * (0.5 - Normal.w)));

	color = color * color; // Gamma correction (approx pow 2.2)
	ssao = saturate(pow(abs(ssao), power)); // Increase ssao contrast
	float3 N = normalize(Normal.xyz);

	// For shadeless areas, make ssao 1
	//ssao = shadeless ? 1.0 : ssao;
	ssao = lerp(ssao, 1.0, shadeless);
	if (ssao_debug) {
		ssao = sqrt(ssao); // Gamma correction
		output.color = float4(ssao, 1);
		return output;
	}
	// Toggle the SSAO component for debugging purposes:
	ssao = lerp(ssao, 1.0, sso_disable);

	bool FGFlag;
	float3 P1 = getPositionFG(input.uv, 0);
	float3 P2 = getPositionBG(input.uv, 0);
	if (P1.z < P2.z) {
		pos3D = P1;
		FGFlag = true;
	}
	else {
		pos3D = P2;
		FGFlag = false;
	}

	// Fade shading with distance: works for Yavin, doesn't work for large space missions with planets on them
	// like "Enemy at the Gates"... so maybe enable distance_fade for planetary missions? Those with skydomes...
	float distance_fade = enable_dist_fade * saturate((pos3D.z - INFINITY_Z0) / INFINITY_FADEOUT_RANGE);
	shadeless = saturate(lerp(shadeless, 1.0, distance_fade));

	/*
	if (fn_enable) {
		float nm_intensity = lerp(nm_intensity_near, nm_intensity_far, saturate(pos3D.z / 4000.0));
		float3 FakeNormal = get_normal_from_color(input.uv, offset);
		n = blend_normals(nm_intensity * FakeNormal, n);
		ssao *= (1 - n.z);
	}

	ssao = lerp(ssao, 1, mask);
	if (debug)
		return float4(ssao, 1);
	else
		return float4(color * ssao, 1);
	*/

	//float2 offset = float2(pixelSizeX, pixelSizeY);
	float2 offset = float2(1.0 / screenSizeX, 1.0 / screenSizeY);
	float3 FakeNormal = 0;
	// Glass, Shadeless and Emission should not have normal mapping:
	if (fn_enable && mask < GLASS_LO) {
		//nm_int = lerp(nm_intensity_near, nm_intensity_far, saturate(pos3D.z / 4000.0));
		FakeNormal = get_normal_from_color(input.uv, offset, nm_int);
		N = blend_normals(N, FakeNormal);
	}

	// We need to invert the Z-axis for illumination because the normals are Z+ when viewing the camera
	// so that implies that Z increases towards the viewer and decreases away from the camera.
	// We could also avoid inverting Z in PixelShaderTexture... but then we also need to invert the fake
	// normals.
	pos3D.z = -pos3D.z;

	// Specular color
	float3 spec_col = 1.0;
	float3 HSV = RGBtoHSV(color);
	// Handle both plastic and metallic materials
	if (mask < METAL_HI) {
		// The tint varies from 0 for plastic materials to 1 for fully metallic mats
		float tint = lerp(0.0, 1.0, metallic);
		float value = lerp(spec_val, 0.0, metallic);
		diff_int = lerp(1.0, 0.0, metallic); // Purely metallic surfaces have no diffuse component (?)
		HSV.y *= tint * saturation_boost;
		HSV.z = HSV.z * lightness_boost + value;
		spec_col = HSVtoRGB(HSV);
	}

	float3 tmp_color = 0.0;
	float4 tmp_bloom = 0.0;
	// Compute the shading contribution from the main lights
	[unroll]
	for (uint i = 0; i < LightCount; i++) {
		float3 L = LightVector[i].xyz; // Lights come with Z inverted from ddraw, so they expect negative Z values in front of the camera
		float LightIntensity = dot(LightColor[i].rgb, 0.333);
		// diffuse component
		float diffuse = max(dot(N, L), 0.0);
		diffuse = ssao.x * diff_int * diffuse + ambient;
		//diffuse = (diff_max * diff_int * diffuse) + (ssao.x * amb_max * ambient);
		//diffuse = diff_int * diffuse + ambient;

		//diffuse = lerp(diffuse, 1, mask); // This applies the shadeless material; but it's now defined differently
		//diffuse = shadeless ? 1.0 : diffuse;
		diffuse = lerp(diffuse, 1.0, shadeless);

		// specular component
		//float3 eye = 0.0;
		//float3 spec_col = lerp(min(6.0 * color, 1.0), 1.0, mask); // Force spec_col to be white on masked (DC) areas
		//float3 spec_col = 0.35;
		float3 eye_vec = normalize(-pos3D); // normalize(eye - pos3D);
		// reflect expects an incident vector: a vector that goes from the light source to the current point.
		// L goes from the current point to the light vector, so we have to use -L:
		float3 refl_vec = normalize(reflect(-L, N));
		float  spec = max(dot(eye_vec, refl_vec), 0.0);

		//float3 viewDir    = normalize(-pos3D);
		//float3 halfwayDir = normalize(L + viewDir);
		//float spec = max(dot(N, halfwayDir), 0.0);

		// We can't have exponent == 0 or we'll see a lot of shading artifacts:
		float exponent = max(global_glossiness * gloss_mask, 0.05);
		float spec_bloom_int = global_spec_bloom_intensity;
		if (GLASS_LO <= mask && mask < GLASS_HI) {
			exponent *= 2.0;
			spec_bloom_int *= 3.0; // Make the glass bloom more
		}
		float spec_bloom = spec_int_mask * spec_bloom_int * pow(spec, exponent * global_bloom_glossiness_mult);
		spec = LightIntensity * spec_int_mask * pow(spec, exponent);

		// The following lines MAY be an alternative to remove spec on shadeless surfaces; keeping glass
		// intact
		//spec_col = mask > SHADELESS_LO ? 0.0 : spec_col;
		//spec_bloom = mask > SHADELESS_LO ? 0.0 : spec_bloom;

		//color = color * ssdo + ssdoInd + ssdo * spec_col * spec;
		tmp_color += LightColor[i].rgb * (color * diffuse + global_spec_intensity * spec_col * spec);
		tmp_bloom += float4(LightIntensity * spec_col * spec_bloom, spec_bloom);
	}
	output.bloom = tmp_bloom;

	// Add the laser lights
#define L_FADEOUT_A_0 30.0
#define L_FADEOUT_A_1 50.0
#define L_FADEOUT_B_0 50.0
#define L_FADEOUT_B_1 1000.0
	float3 laser_light_sum = 0.0;
	//float laser_light_alpha = 0.0;
	[loop]
	for (i = 0; i < num_lasers; i++)
	{
		// P is the original point
		// pos3D = (P.xy, -P.z)
		// LightPoint already comes with z inverted (-z) from ddraw
		float3 L = LightPoint[i].xyz - pos3D;
		const float Z = -LightPoint[i].z;

		const float distance_sqr = dot(L, L);
		L *= rsqrt(distance_sqr); // Normalize L
		// calculate the attenuation
		const float depth_attenuation_A = smoothstep(L_FADEOUT_A_1, L_FADEOUT_A_0, Z); // Fade the cockpit flash quickly
		const float depth_attenuation_B = 0.1 * smoothstep(L_FADEOUT_B_1, L_FADEOUT_B_0, Z); // Fade the distant flash slowly
		const float depth_attenuation = max(depth_attenuation_A, depth_attenuation_B);
		//const float sqr_attenuation_faded = lerp(sqr_attenuation, 0.0, 1.0 - depth_attenuation);
		//const float sqr_attenuation_faded = lerp(sqr_attenuation, 1.0, saturate((Z - L_SQR_FADE_0) / L_SQR_FADE_1));
		const float attenuation = 1.0 / (1.0 + sqr_attenuation * distance_sqr);
		// compute the diffuse contribution
		const float diff_val = max(dot(N, L), 0.0); // Compute the diffuse component
		//laser_light_alpha += diff_val;
		// add everything up
		laser_light_sum += depth_attenuation * attenuation * diff_val * LightPointColor[i].rgb;
	}
	tmp_color += laser_light_intensity * laser_light_sum;

	output.color = float4(sqrt(tmp_color), 1); // Invert gamma correction (approx pow 1/2.2)
	return output;
}
