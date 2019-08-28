#include "BloomCommon.fxh"

// Based on the implementation from:
// https://learnopengl.com/Advanced-Lighting/Bloom

// This texture slot should be the original backbuffer SRV
Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);
// This texture slot should be the bloom texture
Texture2D bloomTex: register(t1);
SamplerState bloomSampler : register(s1);

cbuffer ConstantBuffer : register(b1)
{
	float pixelSizeX, pixelSizeY, colorMul, alphaMul;
	// 16 bytes
	float amplifyFactor, bloomStrength, unused, unused3;
	// 32 bytes
};

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
	float scale = 1.0 / amplifyFactor; // Enlarge image by this much
	float2 input_uv_sub = input.uv * scale;
	float4 color = texture0.Sample(sampler0, input.uv);
	float4 bloom = float4(bloomTex.Sample(bloomSampler, input_uv_sub).xyz, 1);
	//color += bloom;
	//color += bloom.w * bloom;
	color.w = 1.0f;
	//return color;
	// hack
	//bloom.w = 1.0f;
	// Screen blending mode, see http://www.deepskycolors.com/archive/2010/04/21/formulas-for-Photoshop-blending-modes.html
	// 1 - (1 - Target) * (1 - Blend)
	//return color + bloomStrength * bloom;
	return 1 - (1 - color) * (1 - bloomStrength * bloom);
}