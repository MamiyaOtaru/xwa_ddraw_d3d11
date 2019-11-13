/*
 * Simple shader to add SSAO + Bloom-Mask + Color Buffer
 * HDR version
 * Copyright 2019, Leo Reyes.
 * Licensed under the MIT license. See LICENSE.txt
 *
 * The bloom-mask is *not* the final bloom buffer -- this mask is output by the pixel
 * shader and it will be used later to compute proper bloom. Here we use this mask to
 * disable areas of the SSAO buffer that should be bright.
 */
#include "..\HSV.h"

 // The color buffer
Texture2D texColor : register(t0);
SamplerState sampColor : register(s0);

// The bloom mask buffer
Texture2D texBloom : register(t1);
SamplerState samplerBloom : register(s1);

// The bent normal buffer -- This is the SSDO buffer for LDR rendering
Texture2D texBent : register(t2);
SamplerState samplerBent : register(s2);

// The SSDO Indirect buffer
Texture2D texSSDOInd : register(t3);
SamplerState samplerSSDOInd : register(s3);

// The SSAO mask
Texture2D texSSAOMask : register(t4);
SamplerState samplerSSAOMask : register(s4);

// The position/depth buffer
Texture2D texPos : register(t5);
SamplerState sampPos : register(s5);

// The (Flat) Normals buffer
Texture2D texNormal : register(t6);
SamplerState samplerNormal : register(s6);

#define INFINITY_Z 20000

// We're reusing the same constant buffer used to blur bloom; but here
// we really only use the amplifyFactor to upscale the SSAO buffer (if
// it was rendered at half the resolution, for instance)
cbuffer ConstantBuffer : register(b2)
{
	float pixelSizeX, pixelSizeY, unused0, amplifyFactor;
	// 16 bytes
	float bloomStrength, uvStepSize, saturationStrength;
	uint unused1;
	// 32 bytes
	uint unused2;
	float unused3, depth_weight;
	uint debug;
};

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
	float white_point, shadow_step_size, shadow_steps, aspect_ratio;
	// 144 bytes
	float4 vpScale;
	// 160 bytes
	uint shadow_enable;
	float shadow_k, ssao_unused1, ssao_unused2;
	// 176 bytes
};

cbuffer ConstantBuffer : register(b4)
{
	float4 LightVector;
	float4 LightColor;
	float4 LightVector2;
	float4 LightColor2;
};

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD;
};

float3 getPositionFG(in float2 uv, in float level) {
	// The use of SampleLevel fixes the following error:
	// warning X3595: gradient instruction used in a loop with varying iteration
	// This happens because the texture is sampled within an if statement (if FGFlag then...)
	return texPos.SampleLevel(sampPos, uv, level).xyz;
}

/*
 * From Pascal Gilcher's SSR shader.
 * https://github.com/martymcmodding/qUINT/blob/master/Shaders/qUINT_ssr.fx
 * (Used with permission from the author)
 */
float3 get_normal_from_color(float2 uv, float2 offset)
{
	float3 offset_swiz = float3(offset.xy, 0);
	// Luminosity samples
	float hpx = dot(texColor.SampleLevel(sampColor, float2(uv + offset_swiz.xz), 0).xyz, 0.333) * fn_scale;
	float hmx = dot(texColor.SampleLevel(sampColor, float2(uv - offset_swiz.xz), 0).xyz, 0.333) * fn_scale;
	float hpy = dot(texColor.SampleLevel(sampColor, float2(uv + offset_swiz.zy), 0).xyz, 0.333) * fn_scale;
	float hmy = dot(texColor.SampleLevel(sampColor, float2(uv - offset_swiz.zy), 0).xyz, 0.333) * fn_scale;

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

float3 blend_normals(float3 n1, float3 n2)
{
	//return normalize(float3(n1.xy*n2.z + n2.xy*n1.z, n1.z*n2.z));
	n1 += float3(0, 0, 1);
	n2 *= float3(-1, -1, 1);
	return n1 * dot(n1, n2) / n1.z - n2;
}

/*
 * From:
 * https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
 * (free to use under public domain CC0 or MIT license)
 */
inline float3 ACESFilm(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x*(a*x + b)) / (x*(c*x + d) + e));
}

inline float Reinhard4(float Lin, float Lwhite_sqr) {
	return Lin * (1 + Lin / Lwhite_sqr) / (1 + Lin);
}

inline float3 Reinhard4b(float3 Lin, float Lwhite_sqr) {
	return Lin * (1 + Lin / Lwhite_sqr) / (1 + Lin);
}

inline float3 ReinhardFull(float3 rgb, float Lwhite_sqr) {
	//float3 hsv = RGBtoHSV(rgb);
	//hsv.z = Reinhard4(hsv.z, Lwhite_sqr);
	//return HSVtoRGB(hsv);
	return Reinhard4b(rgb, Lwhite_sqr);
}

float3 ToneMapFilmic_Hejl2015(float3 hdr, float whitePt)
{
	float4 vh = float4(hdr, whitePt);
	float4 va = (1.425 * vh) + 0.05f;
	float4 vf = ((vh * va + 0.004f) / ((vh * (va + 0.55f) + 0.0491f))) - 0.0821f;
	return vf.rgb / vf.www;
}

static float METRIC_SCALE_FACTOR = 25.0;

inline float2 projectToUV(in float3 pos3D) {
	float3 P = pos3D;
	float w = P.z / METRIC_SCALE_FACTOR;
	P.xy = P.xy / P.z;
	// Convert to vertex pos:
	//P.xy = ((P.xy / (vpScale.z * float2(aspect_ratio, 1))) - float2(-0.5, 0.5)) / vpScale.xy;
	// (-1,-1)-(1, 1)
	P.xy /= (vpScale.z * float2(aspect_ratio, 1));
	P.xy -= float2(-0.5, 0.5);
	P.xy /= vpScale.xy;
	// We now have P = input.pos
	P.x = (P.x * vpScale.x - 1.0f) * vpScale.z;
	P.y = (P.y * vpScale.y + 1.0f) * vpScale.z;
	//P *= 1.0f / w; // Don't know if this is 100% necessary... probably not

	// Now convert to UV coords: (0, 1)-(1, 0):
	//P.xy = lerp(float2(0, 1), float2(1, 0), (P.xy + 1) / 2);
	// The viewport used to render the original offscreenBuffer may not cover the full
	// screen, so the uv coords have to be adjusted to the limits of the viewport within
	// the full-screen quad:
	P.xy = lerp(float2(x0, y1), float2(x1, y0), (P.xy + 1) / 2);
	return P.xy;
}

float3 shadow_factor(in float3 P, float max_dist_sqr) {
	float3 cur_pos = P, occluder, diff;
	float2 cur_uv;
	float3 ray_step = shadow_step_size * LightVector.xyz;
	int steps = (int)shadow_steps;
	float max_shadow_length = shadow_step_size * shadow_steps;
	float max_shadow_length_sqr = max_shadow_length * 0.75; // Fade the shadow a little before it reaches a hard edge
	max_shadow_length_sqr *= max_shadow_length_sqr;
	float cur_length = 0, length_at_res = INFINITY_Z;
	float res = 1.0;
	float weight = 1.0;
	//float occ_dot;

	// Handle samples that land outside the bounds of the image
	// "negative" cur_diff should be ignored
	[loop]
	for (int i = 1; i <= steps; i++) {
		cur_pos    += ray_step;
		cur_length += shadow_step_size;
		cur_uv		= projectToUV(cur_pos);

		// If the ray has exited the current viewport, we're done:
		if (cur_uv.x < x0 || cur_uv.x > x1 ||
			cur_uv.y < y0 || cur_uv.y > y1) {
			weight = saturate(1 - length_at_res * length_at_res / max_shadow_length_sqr);
			res = lerp(1, res, weight);
			return float3(res, cur_length / max_shadow_length, 1);
		}

		occluder = texPos.SampleLevel(sampPos, cur_uv, 0).xyz;
		diff		 = occluder - cur_pos;
		//v        = normalize(diff);
		//occ_dot  = max(0.0, dot(LightVector.xyz, v) - bias);

		if (diff.z > 0 /* && diff.z < max_dist */) { // Ignore negative z-diffs: the occluder is behind the ray
			// If diff.z is too large, ignore it. Or rather, fade with distance
			//float weight = saturate(1.0 - (diff.z * diff.z / max_dist_sqr));
			//float dist = saturate(lerp(1, diff.z), weight);
			float cur_res = saturate(shadow_k * diff.z / (cur_length + 0.00001));
			//cur_res = saturate(lerp(1, cur_res, weight)); // Fadeout if diff.z is too big
			if (cur_res < res) {
				res = cur_res;
				length_at_res = cur_length;
			}
		}

		//cur_pos += ray_step;
		//cur_length += shadow_step_size;
	}
	weight = saturate(1 - length_at_res * length_at_res / max_shadow_length_sqr);
	res = lerp(1, res, weight);
	return float3(res, cur_length / max_shadow_length, 0);
}

float4 main(PixelShaderInput input) : SV_TARGET
{
	float2 input_uv_sub = input.uv * amplifyFactor;
	//float2 input_uv_sub2 = input.uv * amplifyFactor2;
	float2 input_uv_sub2 = input.uv * amplifyFactor;
	float3 albedo = pow(abs(texColor.Sample(sampColor, input.uv).xyz), gamma);
	float3 bentN = texBent.Sample(samplerBent, input_uv_sub).xyz; // Looks like bentN is already normalized
	float3 pos3D = texPos.Sample(sampPos, input.uv).xyz;
	float3 Normal = texNormal.Sample(samplerNormal, input.uv).xyz;
	float4 bloom = texBloom.Sample(samplerBloom, input.uv);
	//float3 ssdo = texSSDO.Sample(samplerSSDO, input_uv_sub).rgb; // HDR
	float3 ssdoInd = texSSDOInd.Sample(samplerSSDOInd, input_uv_sub2).rgb;
	float3 ssaoMask = texSSAOMask.Sample(samplerSSAOMask, input.uv).xyz;
	//float  mask = max(dot(0.333, bloom.xyz), dot(0.333, ssaoMask));
	float mask = dot(0.333, ssaoMask);

	// Early exit: don't touch the background
	if (pos3D.z > INFINITY_Z) return float4(pow(abs(albedo), 1 / gamma), 1);

	// Apply Normal Mapping
	if (fn_enable) {
		float2 offset = float2(1 / screenSizeX, 1 / screenSizeY);
		float nm_intensity = lerp(nm_intensity_near, nm_intensity_far, saturate(pos3D.z / 4000.0));
		float3 FakeNormal = get_normal_from_color(input.uv, offset);
		bentN = blend_normals(nm_intensity * FakeNormal, bentN);
	}

	float m_offset = max(moire_offset, moire_offset * (pos3D.z * 0.1));
	//pos3D.z -= m_offset;
	pos3D += Normal * m_offset;

	// Compute shadows
	//float max_dist_sqr = max_dist * max_dist;
	//float3 shadow = max(ambient, shadow_factor(pos3D, max_dist_sqr));
	//if (!shadow_enable)
	//	shadow = 1;
	float3 shadow = 1;

	if (ssao_debug) {
		//float res = shadow.x;
		//res = 1 - res;
		//float3 color = float3(res, shadow.y, 0) * albedo;
		//float3 color = float3(0, res * shadow.y, 0);
		//return float4(pow(abs(color), 1 / gamma), 1);
		return float4(shadow.xxx, 1);
		// Display the uv coords:
		//float2 uv = projectToUV(pos3D);
		//return float4(uv, 0.5, 1);
	}
	else
		shadow = shadow.xxx;

	float3 reflected = reflect(LightVector.xyz, Normal);
	float3 eyeVector = 0 - pos3D;
	//float3 eyeVector = float3(0, 0, -1);
	float spec = saturate(dot(reflected, eyeVector));
	//spec = pow(abs(spec), white_point); // TODO: Use something different for the specular highlight
	//float3 specColor = float3(1, 1, 0);
	float3 temp = ambient;
	//temp += LightColor.rgb  * saturate(dot(bentN,  LightVector.xyz));
	//temp += invLightColor   * saturate(dot(bentN, -LightVector.xyz));
	//temp += LightColor2.rgb * saturate(dot(bentN,  LightVector2.xyz));
	temp += LightColor.rgb * saturate(dot(Normal, LightVector.xyz)) * shadow; // + (shadow * spec * specColor);
	//temp += LightColor.rgb * saturate(dot(bentN, LightVector.xyz)) * shadow; // + (shadow * spec * specColor);
	//temp *= shadow;
	//if (shadow > 0) temp = float3(1, 0, 0);
	float3 color = saturate(albedo * temp);
	color = lerp(color, albedo, mask * 0.75);
	return float4(pow(abs(color), 1 / gamma), 1);
	//return float4(projectToUV(pos3D), 0, 1);

	//ssdo = ambient + ssdo; // Add the ambient component
	// Apply tone mapping:
	//ssdo = ssdo / (ssdo + 1);
	//ssdo = pow(abs(ssdo), 1.0 / gamma);
	//return float4(ssdo, 1);
	//float3 hsv = RGBtoHSV(color);
	//float level = dot(0.333, ssdo);
	//hsv.z = saturate(level);
	//color = HSVtoRGB(hsv);
	//return float4(ssdo, 1);
}
