// Based on Pascal Gilcher's MXAO implementation and on
// the following:
// https://www.gamedev.net/articles/programming/graphics/a-simple-and-practical-approach-to-ssao-r2753/
// Adapted for XWA by Leo Reyes.
// Licensed under the MIT license. See LICENSE.txt

// The Foreground 3D position buffer (linear X,Y,Z)
Texture2D    texPos   : register(t0);
SamplerState sampPos  : register(s0);

// The Background 3D position buffer (linear X,Y,Z)
Texture2D    texPos2  : register(t1);
SamplerState sampPos2 : register(s1);

// The normal buffer
Texture2D    texNorm   : register(t2);
SamplerState sampNorm  : register(s2);

// The color buffer
Texture2D    texColor  : register(t3);
SamplerState sampColor : register(s3);

#define INFINITY_Z 10000

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

struct PixelShaderOutput
{
	float4 ssao        : SV_TARGET0;
	//float4 bentNormal  : SV_TARGET1; // Bent normal map output
};

cbuffer ConstantBuffer : register(b3)
{
	float screenSizeX, screenSizeY, scale, bias;
	// 16 bytes
	float intensity, sample_radius, black_level;
	uint samples;
	// 32 bytes
	uint z_division;
	float area, falloff, power;
	// 48 bytes
};

//interface IPosition {
//	float3 getPosition(in float2 uv);
//};

//class ForegroundPos : IPosition {
float3 getPositionFG(in float2 uv) {
	// The use of SampleLevel fixes the following error:
	// warning X3595: gradient instruction used in a loop with varying iteration
	// This happens because the texture is sampled within an if statement (if FGFlag then...)
	return texPos.SampleLevel(sampPos, uv, 0).xyz;
}
//};

//class BackgroundPos : IPosition {
float3 getPositionBG(in float2 uv) {
	return texPos2.SampleLevel(sampPos2, uv, 0).xyz;
}
//};

inline float3 getNormal(in float2 uv) {
	return texNorm.Sample(sampNorm, uv).xyz;
}

inline float3 doAmbientOcclusion(bool FGFlag, in float2 uv, in float3 P, in float3 Normal /*, inout float3 BentNormal */)
{
	//float3 color   = texColor.Sample(sampColor, tcoord + uv).xyz;
	//float3 occluderNormal = getNormal(uv + uv_offset).xyz;
	float3 occluder = FGFlag ? getPositionFG(uv) : getPositionBG(uv);
	// diff: Vector from current pos (p) to sampled neighbor
	float3 diff = occluder - P;
	//float zdist     = -diff.z;
	// L: Distance from current pos (P) to the occluder
	const float  L = length(diff);
	// v: Normalized (occluder - P) vector
	const float3 v = diff / L;
	const float  d = L * scale;
	//if (zdist < 0) BentNormal += v;
	//BentNormal += v;
	return intensity * pow(max(0.0, dot(Normal, v) - bias), power) / (1.0 + d);
}

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	output.ssao = float4(1, 1, 1, 1);
	//float3 dummy = float3(0, 0, 0);
	
	float3 P1 = getPositionFG(input.uv);
	float3 P2 = getPositionBG(input.uv);
	float3 n = getNormal(input.uv);
	//float3 bentNormal = n;
	float3 ao = float3(0.0, 0.0, 0.0);
	float radius = sample_radius;
	float3 p;
	bool FGFlag;
	
	if (P1.z < P2.z) {
		p = P1;
		FGFlag = true;
	} else {
		p = P2;
		FGFlag = false;
	}
	
	// Early exit: do not compute SSAO for objects at infinity
	if (p.z > INFINITY_Z) return output;

	// Enable perspective-correct radius
	if (z_division) 	radius /= p.z;

	float sample_jitter = dot(floor(input.pos.xy % 4 + 0.1), float2(0.0625, 0.25)) + 0.0625;
	float2 sample_uv, sample_direction;
	const float2x2 rotMatrix = float2x2(0.76465, -0.64444, 0.64444, 0.76465); //cos/sin 2.3999632 * 16 
	sincos(2.3999632 * 16 * sample_jitter, sample_direction.x, sample_direction.y); // 2.3999632 * 16
	sample_direction *= radius;

	// SSAO Calculation
	[loop]
	for (uint j = 0; j < samples; j++)
	{
		sample_uv = input.uv + sample_direction.xy * (j + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix); 
		ao += doAmbientOcclusion(FGFlag, sample_uv, p, n);

		/*
		i++;

		sample_uv = input.uv + sample_direction.xy * (i + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix);
		ao += doAmbientOcclusion(FGFlag, sample_uv, p, n);
		i++;

		sample_uv = input.uv + sample_direction.xy * (i + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix);
		ao += doAmbientOcclusion(FGFlag, sample_uv, p, n);
		i++;

		sample_uv = input.uv + sample_direction.xy * (i + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix);
		ao += doAmbientOcclusion(FGFlag, sample_uv, p, n);
		i++;
		*/
	}

	ao = 1 - ao / (float)samples;
	output.ssao.xyz *= lerp(black_level, ao, ao);
	//output.bentNormal = float4(normalize(bentNormal), 1);
	return output;
}
