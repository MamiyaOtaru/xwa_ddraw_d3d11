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
	float bentNormalInit, max_dist, power;
	// 48 bytes
	uint debug, unused1, unused2, unused3;
	// 64 bytes
};

struct BlurData {
	float3 pos;
	float3 normal;
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

inline float3 doAmbientOcclusion(bool FGFlag, /* in float2 input_uv, */ in float2 sample_uv, 
	//float cur_radius, float max_radius, 
	in float3 P, in float3 Normal /*, inout float3 BentNormal */)
{
	//float3 color   = texColor.Sample(sampColor, tcoord + uv).xyz;
	//float3 occluderNormal = getNormal(uv + uv_offset).xyz;
	float3 occluder = FGFlag ? getPositionFG(sample_uv) : getPositionBG(sample_uv);
	// diff: Vector from current pos (p) to sampled neighbor
	float3 diff		= occluder - P;
	const float diff_sqr = dot(diff, diff);
	// v: Normalized (occluder - P) vector
	const float3 v = diff * rsqrt(diff_sqr);
	const float max_dist_sqr = max_dist * max_dist;
	//const float weight = smoothstep(0, 1, saturate(max_dist - abs(occluder.z - P.z)));
	const float weight = saturate(1 - diff_sqr / max_dist_sqr);
	//const float  d = L * scale;

	/*if (zdist > 0.0) {
		float2 uv_diff = sample_uv - input_uv;
		float cur_radius2 = cur_radius * cur_radius;
		float3 B = float3(uv_diff.x, -uv_diff.y, sqrt(cur_radius2 - dot(uv_diff, uv_diff)));
		B = normalize(B);
		//float weight = dot(Normal, B);
		//BentNormal += weight * B;
		BentNormal += B;
	}*/

	float ao_dot = max(0.0, dot(Normal, v) - bias);
	float ao_factor = ao_dot * weight;
	//float ao_factor = ao_dot / (1.0 + d);
	//BentNormal += (1 - ao_dot) * v;

	/*
	float2 uv_diff = sample_uv - input_uv;
	float cur_radius2 = max_radius * max_radius;
	//float3 B = float3(uv_diff.x, uv_diff.y, sqrt(cur_radius2 - dot(uv_diff, uv_diff)));
	//float3 B = float3(uv_diff.x, uv_diff.y, -0.1 * min(abs(uv_diff.x), abs(uv_diff.y)));
	float3 B = float3(uv_diff.x, uv_diff.y, 0.1);
	B = normalize(B);
	BentNormal += (1 - ao_factor) * B;
	*/

	// This one works more-or-less OK-ish:
	
	/*
	float2 uv_diff = sample_uv - input_uv;
	float3 B = float3(0, 0, 0);
	if (diff.z > 0.0) {
		//B.x = -uv_diff.x;
		//B.y =  uv_diff.y;
		//B.z =  -(max_radius - cur_radius);
		//B.z = -0.01 * (max_radius - cur_radius) / max_radius;
		//B = normalize(B);
		B = -v;
		BentNormal += B;
	}
	*/
	
	return intensity * pow(ao_factor, power);
}

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	output.ssao = float4(1, 1, 1, 1);
	//output.bentNormal = float4(0, 0, 0, 0);
	
	float3 P1 = getPositionFG(input.uv);
	float3 P2 = getPositionBG(input.uv);
	float3 n  = getNormal(input.uv);
	//float3 bentNormal = float3(0, 0, 0);
	//float3 bentNormal = bentNormalInit * n; // Initialize the bentNormal with the normal
	float3 ao = float3(0.0, 0.0, 0.0);
	float3 p;
	float radius = sample_radius;
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
	float max_radius = radius * (float)(samples - 1 + sample_jitter);
	//float max_radius = radius;

	// SSAO Calculation
	//bentNormal = n;
	[loop]
	for (uint j = 0; j < samples; j++)
	{
		sample_uv = input.uv + sample_direction.xy * (j + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix); 
		ao += doAmbientOcclusion(FGFlag, /* input.uv, */ sample_uv, /* max_radius, */ p, n /*, bentNormal */);
		//ao += doAmbientOcclusion(FGFlag, input.uv, sample_uv, max_radius, p, n, bentNormal);
		//ao += doAmbientOcclusion(FGFlag, input.uv, sample_uv, 
		//	radius * (j + sample_jitter), max_radius, 
		//	p, n, bentNormal);
	}

	ao = 1 - ao / (float)samples;
	output.ssao.xyz *= lerp(black_level, ao, ao);
	
	/*
	float B = length(bentNormal);
	if (B > 0.001) {
		// The version that worked did the following:
		bentNormal = -bentNormal / B;
		
		//bentNormal = bentNormal / B;
		//output.bentNormal = float4(bentNormal, 1);
		//output.bentNormal = float4(bentNormal * 0.5 + 0.5, 1);
		output.bentNormal = float4(bentNormal, 1);
		if (debug == 1)
			output.bentNormal = float4(bentNormal.xxx * 0.5 + 0.5, 1);
		else if (debug == 2)
			output.bentNormal = float4(bentNormal.yyy * 0.5 + 0.5, 1);
		else if (debug == 3)
			output.bentNormal = float4(bentNormal.zzz * 0.5 + 0.5, 1);
		else if (debug == 4)
			output.bentNormal = float4(bentNormal * 0.5 + 0.5, 1);
	}
	//output.bentNormal = float4(bentNormal, 1);
	*/
	return output;
}

/*
// DSSDO
PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	//output.ssao = float4(1, 1, 1, 1);
	output.ssao = debug ? float4(0, 0, 0, 1) : 1;
	output.bentNormal = float4(0, 0, 0, 1);

	float3 P1 = getPositionFG(input.uv);
	float3 P2 = getPositionBG(input.uv);
	float3 n = getNormal(input.uv);
	float3 bentNormal = 0;
	//float3 bentNormal = bentNormalInit * n; // Initialize the bentNormal with the normal
	float3 ao = 0;
	float3 p;
	float radius = sample_radius;
	bool FGFlag;
	float attenuation_angle_threshold = 0.1;
	float4 occlusion_sh2 = 0;
	const float fudge_factor_l0 = 2.0;
	const float fudge_factor_l1 = 10.0;
	const float  sh2_weight_l0 = fudge_factor_l0 * 0.28209; // 0.5 * sqrt(1.0/pi);
	const float3 sh2_weight_l1 = fudge_factor_l1 * 0.48860; // 0.5 * sqrt(3.0/pi);
	const float4 sh2_weight    = float4(sh2_weight_l1, sh2_weight_l0) / samples;

	if (P1.z < P2.z) {
		p = P1;
		FGFlag = true;
	}
	else {
		p = P2;
		FGFlag = false;
	}

	// Early exit: do not compute SSAO for objects at infinity
	if (p.z > INFINITY_Z) return output;

	float sample_jitter = dot(floor(input.pos.xy % 4 + 0.1), float2(0.0625, 0.25)) + 0.0625;
	float2 sample_uv, sample_direction;
	const float2x2 rotMatrix = float2x2(0.76465, -0.64444, 0.64444, 0.76465); //cos/sin 2.3999632 * 16 
	sincos(2.3999632 * 16 * sample_jitter, sample_direction.x, sample_direction.y); // 2.3999632 * 16
	sample_direction *= radius;
	//float max_radius = radius * (float)(samples - 1 + sample_jitter);
	float max_distance_inv = 1.0 / max_dist;

	// DSSDO Calculation
	//bentNormal = n;
	[loop]
	for (uint j = 0; j < samples; j++)
	{
		sample_uv = input.uv + sample_direction.xy * (j + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix);
		//ao += doAmbientOcclusion(FGFlag, input.uv, sample_uv, max_radius, p, n, bentNormal);
		float3 sample_pos = FGFlag ? getPositionFG(sample_uv) : getPositionBG(sample_uv);
		float3 center_to_sample = sample_pos - p;
		float  dist = length(center_to_sample);
		float3 center_to_sample_normalized = center_to_sample / dist;
		float  attenuation = 1 - saturate(dist * max_distance_inv);
		float  dp = dot(n, center_to_sample_normalized);

		attenuation    = attenuation * attenuation * step(attenuation_angle_threshold, dp);
		occlusion_sh2 += attenuation * sh2_weight * float4(center_to_sample_normalized, 1);
		ao += attenuation;
		bentNormal    += (1 - attenuation) * center_to_sample_normalized;
	}

	ao = 1 - ao / (float)samples;
	output.ssao.xyz *= lerp(black_level, ao, ao);
	//bentNormal = occlusion_sh2.xyz;
	//bentNormal.xy += occlusion_sh2.xy;
	//bentNormal.z  -= occlusion_sh2.z;
	//bentNormal.xyz = lerp(bentNormal.xyz, n, ao);
	bentNormal = -bentNormal;
	bentNormal.z = -bentNormal.z;
	output.bentNormal.xyz = normalize(bentNormalInit * n - bentNormal);

	if (debug)
		output.bentNormal.xyz = output.bentNormal.xyz * 0.5 + 0.5;

	if (debug == 1) {
		output.ssao.xyz = output.bentNormal.xxx;
	}
	else if (debug == 2) {
		output.ssao.xyz = output.bentNormal.yyy;
	}
	else if (debug == 3) {
		output.ssao.xyz = output.bentNormal.zzz;
	}

	return output;
}
*/