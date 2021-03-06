/*
 * Simple blur for SSAO
 * Copyright 2019, Leo Reyes
 * Licensed under the MIT license. See LICENSE.txt
 */

 // The SSAO Buffer
Texture2D SSAOTex : register(t0);
SamplerState SSAOSampler : register(s0);

// The FG Depth Buffer
Texture2D DepthTex : register(t1);
SamplerState DepthSampler : register(s1);

// The BG Depth Buffer
Texture2D DepthTex2 : register(t2);
SamplerState DepthSampler2 : register(s2);

// The Normal Buffer
Texture2D NormalTex : register(t3);
SamplerState NormalSampler : register(s3);

// The Bent Normals
Texture2D BentTex : register(t4);
SamplerState BentSampler : register(s4);

struct BlurData {
	float3 pos;
	float3 normal;
};

// I'm reusing the constant buffer from the bloom blur shader; but
// we're only using the amplifyFactor here.
cbuffer ConstantBuffer : register(b2)
{
	float pixelSizeX, pixelSizeY, unused0, amplifyFactor;
	// 16 bytes
	float bloomStrength, uvStepSize, saturationStrength;
	uint unused1;
	// 32 bytes
	uint unused2;
	float unused3, depth_weight, unused4;
	// 48 bytes
};

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD;
};

struct PixelShaderOutput
{
	float4 ssao : SV_TARGET0;
};

float compute_spatial_tap_weight(in BlurData center, in BlurData tap)
{
	if (tap.pos.z > 30000.0) return 0;
	const float depth_term = saturate(depth_weight - abs(tap.pos.z - center.pos.z));
	const float normal_term = saturate(dot(tap.normal.xyz, center.normal.xyz) * 16 - 15);
	return depth_term * normal_term;
}

#define BLUR_SAMPLES 8
PixelShaderOutput main(PixelShaderInput input) {
	static const float2 offsets[16] =
	{
		float2( 1.5, 0.5), float2(-1.5, -0.5), float2(-0.5, 1.5), float2( 0.5, -1.5),
		float2( 1.5, 2.5), float2(-1.5, -2.5), float2(-2.5, 1.5), float2( 2.5, -1.5),
		float2(-1.5, 0.5), float2( 1.5, -0.5), float2( 0.5, 1.5), float2(-0.5, -1.5),
		float2(-1.5, 2.5), float2( 1.5, -2.5), float2( 2.5, 1.5), float2(-2.5, -1.5),
	};
	float2 cur_offset, cur_offset_scaled;
	float2 pixelSize = float2(pixelSizeX, pixelSizeY);
	float2 input_uv_scaled = input.uv * amplifyFactor;
	float  blurweight = 0, tap_weight;
	BlurData center, tap;
	float3 tap_ssao, ssao_sum, ssao_sum_noweight;
	float3 P = DepthTex.Sample(DepthSampler, input.uv).xyz;
	float3 Q = DepthTex2.Sample(DepthSampler2, input.uv).xyz;
	float3 tapFG, tapBG;
	bool FGFlag = true;
	center.pos = P;
	if (Q.z < P.z) {
		FGFlag = false;
		center.pos = Q;
	}

	PixelShaderOutput output;
	output.ssao = float4(0, 0, 0, 1);

	ssao_sum = SSAOTex.Sample(SSAOSampler, input_uv_scaled).xyz;
	center.normal = NormalTex.Sample(NormalSampler, input.uv).xyz;
	blurweight = 1;
	ssao_sum_noweight = ssao_sum;

	[unroll]
	for (int i = 0; i < BLUR_SAMPLES; i++)
	{
		cur_offset = pixelSize * offsets[i];
		cur_offset_scaled = amplifyFactor * cur_offset;
		tap_ssao = SSAOTex.Sample(SSAOSampler, input_uv_scaled + cur_offset_scaled).xyz;
		if (FGFlag)
			tap.pos = DepthTex.Sample(DepthSampler, input.uv + cur_offset).xyz;
		else
			tap.pos = DepthTex2.Sample(DepthSampler2, input.uv + cur_offset).xyz;
		tap.normal = NormalTex.Sample(NormalSampler, input.uv + cur_offset).xyz;

		tap_weight = compute_spatial_tap_weight(center, tap);

		blurweight += tap_weight;
		ssao_sum += tap_ssao * tap_weight;
		ssao_sum_noweight += tap_ssao;
	}

	ssao_sum /= blurweight;
	ssao_sum_noweight /= BLUR_SAMPLES;

	output.ssao = float4(lerp(ssao_sum, ssao_sum_noweight, blurweight < 2), 1);
	return output;
}
