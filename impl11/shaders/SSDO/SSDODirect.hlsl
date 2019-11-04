/*
 * Screen-Space Directional Occlusion, based on Ritschel's paper with
 * code adapted from Pascal Gilcher's MXAO.
 * Adapted for XWA by Leo Reyes.
 * Licensed under the MIT license. See LICENSE.txt
 */
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

// The ssao mask
Texture2D    texSSAOMask  : register(t4);
SamplerState sampSSAOMask : register(s4);

// The bloom mask
Texture2D    texBloomMask  : register(t5);
SamplerState sampBloomMask : register(s5);

#define INFINITY_Z0 10000
#define INFINITY_Z1 15000
#define INFINITY_FADEOUT_RANGE 5000

//static float3 invLightColor = float3(0.058279, 0.069624, 0.085897);
//static float3 invLightColor = float3(1, 0, 0);

struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

struct PixelShaderOutput
{
	float4 ssao        : SV_TARGET0;
	float4 bentNormal  : SV_TARGET1; // Bent normal map output
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
	uint debug;
	float moire_offset, amplifyFactor;
	uint fn_enable;
	// 64 bytes
	float fn_max_xymult, fn_scale, fn_sharpness, nm_intensity_near;
	// 80 bytes
	float far_sample_radius, nm_intensity_far, ambient, unused3;
	// 96 bytes
	float x0, y0, x1, y1; // Viewport limits in uv space
	// 112 bytes
	float3 invLightColor;
	float unused4;
	// 128 bytes
};

cbuffer ConstantBuffer : register(b4)
{
	//matrix projEyeMatrix;
	//matrix viewMatrix;
	//matrix fullViewMatrix;
	float4 LightVector;
	float4 LightColor;
	float4 LightVector2;
	float4 LightColor2;
};

struct BlurData {
	float3 pos;
	float3 normal;
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

inline float3 getNormal(in float2 uv) {
	return texNorm.Sample(sampNorm, uv).xyz;
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
	n1 += float3( 0,  0, 1);
	n2 *= float3(-1, -1, 1);
	return n1 * dot(n1, n2) / n1.z - n2;
}

struct ColNorm {
	float3 col;
	float3 N;
};

inline ColNorm doSSDODirect(bool FGFlag, in float2 input_uv, in float2 sample_uv, in float3 color,
	in float3 P, in float3 Normal,
	in float cur_radius, in float max_radius,
	in float3 FakeNormal, in float nm_intensity)
{
	ColNorm output;
	output.col = 0;
	output.N   = 0;

	// Early exit: darken the edges of the effective viewport
	if (sample_uv.x < x0 || sample_uv.x > x1 ||
		sample_uv.y < y0 || sample_uv.y > y1) {
		return output;
	}
	float2 uv_diff = sample_uv - input_uv;
	
	//float miplevel = L / max_radius * 3; // Don't know if this miplevel actually improves performance
	float miplevel = cur_radius / max_radius * 4; // Is this miplevel better than using L?

	//output.was_sampled = 0;
	float3 occluder = FGFlag ? getPositionFG(sample_uv, miplevel) : getPositionBG(sample_uv, miplevel);
	//if (occluder.z > INFINITY_Z) // The sample is at infinity, don't compute any SSDO
	//	return output;
	//output.was_sampled = occluder.z < INFINITY_Z;
	// diff: Vector from current pos (P) to the sampled neighbor
	//       If the occluder is farther than P, then diff.z will be positive
	//		 If the occluder is closer than P, then  diff.z will be negative
	const float3 diff = occluder - P;
	//const float diff_sqr = dot(diff, diff);
	// v: Normalized (occluder - P) vector
	//const float3 v = diff * rsqrt(diff_sqr);
	//const float max_dist_sqr = max_dist * max_dist;
	//const float weight = saturate(1 - diff_sqr / max_dist_sqr);

	//float ao_dot = max(0.0, dot(Normal, v) - bias);
	//float ao_factor = ao_dot * weight;
	// This formula is wrong; but it worked pretty well:
	//float visibility = saturate(1 - step(bias, ao_factor));
	//float visibility = (1 - ao_dot);
	
	float3 B = 0;
	if (diff.z > 0.0 /* || abs(diff.z) > max_dist */) // the abs() > max_dist term creates a white halo when foreground objects occlude shaded areas!
	//if (diff.z > 0.0 || weight < 0.1) // || abs(diff.z) > max_dist) // occluder is farther than P -- no occlusion; distance between points is too big -- no occlusion, visibility is 1.
	//if (diff.z > 0.0 || weight) // occluder is farther than P -- no occlusion, visibility is 1.
	{
		B.x =  uv_diff.x;
		B.y = -uv_diff.y;
		//B.z = 0.01 * (max_radius - cur_radius) / max_radius;
		//B.z = sqrt(max_radius * max_radius - cur_radius * cur_radius);
		B.z = 0.1 * sqrt(max_radius * max_radius - cur_radius * cur_radius);
		//B.z = 0.1;
		//B = -v;
		// Adding the normalized B to BentNormal seems to yield better normals
		// if B is added to BentNormal before normalization, the resulting normals
		// look more faceted
		B = normalize(B);
		output.N = B;
		if (fn_enable) B = blend_normals(nm_intensity * FakeNormal, B); // This line can go before or after normalize(B)
		//BentNormal += B;
		// I think we can get rid of the visibility term and just return the following
		// from this case or 0 outside this "if" block.
		output.col  = LightColor.rgb  * saturate(dot(B, LightVector.xyz))  + invLightColor * saturate(dot(B, -LightVector.xyz));
		output.col += LightColor2.rgb * saturate(dot(B, LightVector2.xyz)); // +invLightColor * saturate(dot(B, -LightVector2.xyz));
		return output;
	}
	output.N = 0;
	//output.col = visibility * saturate(dot(B, light));
	output.col = 0;
	//output.col = saturate(dot(Normal, light)); // Computing this causes illumination artifacts, set col = 0 instead to have consistent results
	return output;

	//return visibility;
	//return result + color * ao_factor * saturate(dot(Normal, light));
	//return color * saturate(dot(Normal, light));

	/*
	//float3 ao = ao_factor;
	//if (ao_factor > 0.1)
	{
		float3 occluder_col = texColor.SampleLevel(sampColor, sample_uv, 0).xyz;
		float3 occluder_N	= texNorm.SampleLevel(sampNorm, sample_uv, 0).xyz;
		float3 diffuse		= texDiff.SampleLevel(sampDiff, sample_uv, 0).xyz;
		float occ_normal_factor = saturate(dot(Normal, occluder_N));
		//float occ_light_factor = dot(occluder_N, light);
		//float diff_factor = dot(0.333, diffuse);
		float diff_factor = diffuse.r;
		//occluder_col *= saturate(ssdo_area * occ_normal_factor * occ_light_factor * rsqrt(diff_sqr));
		//ssdo += occluder_col * occ_normal_factor * (1 - ao_dot) * weight * diff_factor;
		//ssdo += occluder_col * occ_normal_factor * weight * diff_factor;
		//ssdo += occluder_col * ao_factor * occ_normal_factor * diff_factor;
		ssdo += occluder_col * ao_factor * diff_factor;
	}
	ssdo = ssdo_area * ssdo;
	return intensity * ao;
	//float3 ssdo = intensity * ssdo_area * ao_factor * occ_light_factor * occ_cur_factor * occluder_col;
	//return intensity * lerp(ao, ssdo, ao_factor);
	//return intensity * pow(ao_factor, power);
	*/
}

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	ColNorm ssdo_aux;
	float3 P1 = getPositionFG(input.uv, 0);
	float3 P2 = getPositionBG(input.uv, 0);
	float3 n = getNormal(input.uv);
	float3 color = texColor.SampleLevel(sampColor, input.uv, 0).xyz;
	float ssao_mask = texSSAOMask.SampleLevel(sampSSAOMask, input.uv, 0).x;
	float3 bloom_mask_rgb = texBloomMask.SampleLevel(sampBloomMask, input.uv, 0).rgb;
	float bloom_mask = dot(0.333, bloom_mask_rgb);
	//float3 bentNormal = float3(0, 0, 0);
	// A value of bentNormalInit == 0.2 seems to work fine.
	float3 bentNormal = bentNormalInit * n; // Initialize the bentNormal with the normal
	//float3 bentNormal = float3(0, 0, 0.01);
	float3 ssdo;
	float3 p;
	float radius;
	bool FGFlag;
	
	output.ssao = 1;
	output.bentNormal = float4(0, 0, 0.01, 1);

	if (P1.z < P2.z) {
		p = P1;
		FGFlag = true;
	}
	else {
		p = P2;
		FGFlag = false;
	}
	// This apparently helps prevent z-fighting noise
	//p += 0.01 * n;
	float m_offset = max(moire_offset, moire_offset * (p.z * 0.1));
	//p += m_offset * n;
	p.z -= m_offset;
	//p += m_offset * n;

	// Early exit: do not compute SSDO for objects at infinity
	if (p.z > INFINITY_Z1) return output;

	// Interpolate between near_sample_radius at z == 0 and far_sample_radius at 1km+
	// We need to use saturate() here or we actually get negative numbers!
	radius = lerp(near_sample_radius, far_sample_radius, saturate(p.z / 1000.0));
	float nm_intensity = (1 - ssao_mask) * lerp(nm_intensity_near, nm_intensity_far, saturate(p.z / 4000.0));
	//radius = far_sample_radius;
	// Enable perspective-correct radius
	if (z_division) 	radius /= p.z;

	float2 offset = float2(1 / screenSizeX, 1 / screenSizeY);
	float3 FakeNormal = 0; 
	if (fn_enable) FakeNormal = get_normal_from_color(input.uv, offset);

	float sample_jitter = dot(floor(input.pos.xy % 4 + 0.1), float2(0.0625, 0.25)) + 0.0625;
	float2 sample_uv, sample_direction;
	const float2x2 rotMatrix = float2x2(0.76465, -0.64444, 0.64444, 0.76465); //cos/sin 2.3999632 * 16 
	sincos(2.3999632 * 16 * sample_jitter, sample_direction.x, sample_direction.y); // 2.3999632 * 16
	// After sincos, sample_direction is unitary and 2D
	sample_direction *= radius;
	// Then we multiply it by "radius", so now sample_direction has length "radius"
	float max_radius = radius * (float)(samples + sample_jitter); // Using samples - 1 causes imaginary numbers in B.z (because of the sqrt)

	// SSDO Direct Calculation
	ssdo = 0;
	//uint num_samples = 0;
	[loop]
	for (uint j = 0; j < samples; j++)
	{
		sample_uv = input.uv + sample_direction.xy * (j + sample_jitter);
		sample_direction.xy = mul(sample_direction.xy, rotMatrix);
		ssdo_aux = doSSDODirect(FGFlag, input.uv, sample_uv, color,
			p, n, radius * (j + sample_jitter), max_radius,
			FakeNormal, nm_intensity);
		//if (ssdo_aux.was_sampled) {
		ssdo += ssdo_aux.col;
		bentNormal += ssdo_aux.N;
		//num_samples++;
	}
	//num_samples = max(1, num_samples);
	ssdo = intensity * ssdo / (float)samples;
	if (bloom_mask < 0.975) bloom_mask = 0.0; // Only inhibit SSDO when bloom > 0.975
	ssdo = lerp(ssdo, 1, bloom_mask);
	ssdo = pow(abs(ssdo), power);
	// Start fading the effect at INFINITY_Z0 and fade out completely at INFINITY_Z1
	output.ssao.xyz = lerp(ssdo, 1, saturate((p.z - INFINITY_Z0) / INFINITY_FADEOUT_RANGE));
	//bentNormal /= (float)samples;
	//float BLength = length(bentNormal);
	//ssdo = intensity * saturate(dot(bentNormal, light));
	//output.ssao.rgb = ssdo;
	
	//if (fn_enable) bentNormal = blend_normals(nm_intensity * FakeNormal, bentNormal); // bentNormal is not really used, it's just for debugging.
	//bentNormal /= BLength; // Bent Normals are not supposed to get normalized
	output.bentNormal.xyz = bentNormal * 0.5 + 0.5;
	//output.bentNormal.xyz = radius * 100; // DEBUG! Use this to visualize the radius
	//output.bentNormal.xyz = bentNormal;
	//output.bentNormal.xyz = BLength;
	return output;

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