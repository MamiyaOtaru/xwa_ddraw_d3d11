/*
 * Sun/Star Shader.
 *
 * You can use this shader under the terms of the MIT license, see LICENSE.TXT
 * (free to use even in commercial projects, attribution required)
 *
 * from: https://www.shadertoy.com/view/XdfXRX
 * musk's lense flare, modified by icecool.
 * See the original at: https://www.shadertoy.com/view/4sX3Rs
 *
 * (c) Leo Reyes, 2020.
 */

#include "ShaderToyDefs.h"
#include "shading_system.h"

 // The background texture
Texture2D    bgTex     : register(t0);
SamplerState bgSampler : register(s0);

// The depth buffer: we'll use this as a mask since the sun should be at infinity
Texture2D    depthTex     : register(t1);
SamplerState depthSampler : register(s1);

#define INFINITY_Z 30000.0

// DEBUG
#define cursor_radius 0.04
#define thickness 0.02 //0.007
#define scale 2.0
// DEBUG

/*
	// saturation test:

	vec3 col = vec3(0.0, 0.0, 1.0);
	float g = dot(vec3(0.333), col);
	vec3 gray = vec3(g);
	// saturation test
	float sat = 2.0;
	vec3 final_col = gray + sat * (col - gray);

 */

// ShadertoyCBuffer
cbuffer ConstantBuffer : register(b7)
{
	float iTime, twirl, bloom_strength, srand;
	// 16 bytes
	float2 iResolution;
	uint bDirectSBS;
	float y_center;
	// 32 bytes
	float2 p0, p1; // Limits in uv-coords of the viewport
	// 48 bytes
	matrix viewMat;
	// 112 bytes
	uint bDisneyStyle, hyperspace_phase;
	float tunnel_speed, FOVscale;
	// 128 bytes
	float SunX, SunY, sun_intensity, st_unused1;
	// 144 bytes
};

struct PixelShaderInput
{
	float4 pos    : SV_POSITION;
	float4 color  : COLOR0;
	float2 uv     : TEXCOORD0;
	float4 pos3D  : COLOR1;
};

struct PixelShaderOutput
{
	float4 color    : SV_TARGET0;
};

/*
float sdLine(in vec2 p, in vec2 a, in vec2 b)
{
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return length(pa - ba * h);
}
*/

/*
float noise(float t)
{
	return texture(iChannel0, vec2(t, 0.0) / iChannelResolution[0].xy).x;
}

float noise(vec2 t)
{
	return texture(iChannel0, (t + vec2(iTime)) / iChannelResolution[0].xy).x;
}
*/

/*
// Simple star lens flare
float lensflare(vec2 uv, vec2 pos, float flare_size, float ang_offset)
{
	vec2 main = uv - pos;
	float dist = length(main);
	float num_points = 2.71;
	float disk_size = 0.2;
	float inv_size = 1.0 / flare_size;
	float ang = atan2(main.y, main.x) + ang_offset;

	float f0 = 1.0 / (dist * inv_size + 1.0);
	f0 = f0 + f0 * (0.1 * sin((sin(ang*4.0 + pos.x)*4.0 - cos(ang*3.0 + pos.y)) * num_points) + disk_size);
	return f0;
}
*/

// Full lens flare (the star spikes have been commented out)
vec3 lensflare(vec2 coord, vec2 flare_pos)
{
	vec2 main = coord - flare_pos;
	vec2 uvd = coord * (length(coord));

	float ang = atan2(main.y, main.x);
	float dist = length(main); dist = pow(dist, .1);
	float n = noise(vec2((ang - iTime / 9.0)*16.0, dist*32.0));

	float f0 = 0.0;
	//f0 = 1.0/(length(uv-flare_pos)*16.0+1.0);

	//f0 = f0+f0*(sin((ang+iTime/18.0 + noise(abs(ang)+n/2.0)*2.0)*12.0)*.1+dist*.1+.8);

	float f2  = max(1.0 / (1.0 + 32.0*pow(length(uvd + 0.80 * flare_pos), 2.0)), 0.0) * 0.25;
	float f22 = max(1.0 / (1.0 + 32.0*pow(length(uvd + 0.85 * flare_pos), 2.0)), 0.0) * 0.23;
	float f23 = max(1.0 / (1.0 + 32.0*pow(length(uvd + 0.90 * flare_pos), 2.0)), 0.0) * 0.21;

	vec2 uvx = mix(coord, uvd, -0.5);

	float f4 = max(0.01 - pow(length(uvx + 0.4*flare_pos), 2.4), .0)*6.0;
	float f42 = max(0.01 - pow(length(uvx + 0.45*flare_pos), 2.4), .0)*5.0;
	float f43 = max(0.01 - pow(length(uvx + 0.5*flare_pos), 2.4), .0)*3.0;

	uvx = mix(coord, uvd, -.4);

	float f5 = max(0.01 - pow(length(uvx + 0.2*flare_pos), 5.5), .0)*2.0;
	float f52 = max(0.01 - pow(length(uvx + 0.4*flare_pos), 5.5), .0)*2.0;
	float f53 = max(0.01 - pow(length(uvx + 0.6*flare_pos), 5.5), .0)*2.0;

	uvx = mix(coord, uvd, -0.5);

	float f6 = max(0.01 - pow(length(uvx - 0.3*flare_pos), 1.6), .0)*6.0;
	float f62 = max(0.01 - pow(length(uvx - 0.325*flare_pos), 1.6), .0)*3.0;
	float f63 = max(0.01 - pow(length(uvx - 0.35*flare_pos), 1.6), .0)*5.0;

	vec3 c = 0.0;

	c.r += f2 + f4 + f5 + f6; c.g += f22 + f42 + f52 + f62; c.b += f23 + f43 + f53 + f63;
	//c+=vec3(f0);

	return c;
}

vec3 cc(vec3 color, float factor, float factor2) // color modifier
{
	float w = color.x + color.y + color.z;
	return mix(color, w * factor, w * factor2);
}

float sdCircle(in vec2 p, in vec2 center, float radius)
{
	return length(p - center) - radius;
}

PixelShaderOutput main(PixelShaderInput input) {
	PixelShaderOutput output;
	vec2 fragCoord = input.uv * iResolution.xy;
	vec3 sunPos3D, color = 0.0;
	//float3 pos3D = depthTex.Sample(depthSampler, input.uv).xyz;
	vec2 sunPos = 0.0;
	float d, dm;
	output.color = bgTex.Sample(bgSampler, input.uv);

	// Early exit: avoid rendering outside the original viewport edges, or when we're not rendering at infinity
	if (any(input.uv < p0) || any(input.uv > p1)) // || pos3D.z < INFINITY_Z) We can display the lens flare on top of everything else
		return output;

	vec2 p = (2.0 * fragCoord.xy - iResolution.xy) / min(iResolution.x, iResolution.y);
	//p += vec2(0, y_center); // Use this for light vectors, In XWA the aiming HUD is not at the screen's center in cockpit view
	//vec3 v = vec3(p.x, -p.y, -FOVscale); // Use this for light vectors
	vec3 v = vec3(p, -FOVscale);
	//v = mul(viewMat, vec4(v, 0.0)).xyz;
	//vec3 v = vec3(p, 0.0);
	//vec2 sunPos = (vec2(SunXL, SunYL) - 0.5) * 2.0;
	
	// Use the following then SunXL, SunYL is in desktop-resolution coordinates:
	//vec2 sunPos = (2.0 * vec2(SunXL, SunYL) - iResolution.xy) / min(iResolution.x, iResolution.y);
	
	// Use the following when SunXL,YL is a light vector:
	//vec2 sunPos = -2.35 * vec2(-SunXL.x, SunYL);
	//vec2 sunPos = debugFOV * vec2(-SunXL.x, SunYL);

	sunPos = (2.0 * vec2(SunX, SunY) - iResolution.xy) / min(iResolution.x, iResolution.y);
	// Sample the depth at the location of the sun:
	sunPos3D = depthTex.Sample(depthSampler, vec2(SunX, SunY) / iResolution.xy).xyz;
	// Avoid displaying any flare if the sun is occluded:
	if (sunPos3D.z < INFINITY_Z)
		return output;

	vec3 flare = 2.0 * lensflare(v.xy, sunPos);
	//float intensity = dot(0.333, flare);
	//output.color.rgb = lerp(output.color.rgb, flare, intensity);
	output.color.rgb += flare;
	return output;

	// DEBUG
	float3 col = float3(1.0, 0.0, 0.0); // Reticle color
	d = sdCircle(v.xy, sunPos, scale * cursor_radius);
	dm = smoothstep(thickness, 0.0, abs(d)); // Outer ring
	dm += smoothstep(thickness, 0.0, abs(d + scale * (cursor_radius - 0.001))); // Center dot
	dm = clamp(dm, 0.0, 1.0);
	col *= dm;
	output.color.rgb = lerp(output.color.rgb, col, 0.8 * dm);
	return output;

	// Display each light in the system
	p += vec2(0, y_center);
	[loop]
	for (uint i = 0; i < LightCount; i++)
	{
		if (LightVector[i].z > 0.0)
			continue; // Skip lights behind the camera
		col = float3(0.0, 1.0, 0.0); // Reticle color
		vec2 sunPos = -2.35 * vec2(-LightVector[i].x, LightVector[i].y);
		v = vec3(p, -FOVscale);
		//v = mul(viewMat, vec4(v, 0.0)).xyz;

		d = sdCircle(v.xy, sunPos, scale * cursor_radius);
		dm = smoothstep(thickness, 0.0, abs(d)); // Outer ring
		dm += smoothstep(thickness, 0.0, abs(d + scale * (cursor_radius - 0.001))); // Center dot
		dm = clamp(dm, 0.0, 1.0);
		col *= dm;
		output.color.rgb = lerp(output.color.rgb, col, 0.8 * dm);
	}
	// DEBUG

	/*
	vec3 light_color = vec3(0.3, 0.3, 1.0);
	// sunPos = 0.0;
	sunPos = (2.0 * vec2(SunX, SunY) - iResolution.xy) / min(iResolution.x, iResolution.y);
	vec2 dcenter = v.xy - sunPos;
	const float V_2 = dot(dcenter, dcenter);
	//const float disk = saturate(pow(0.01 / V_2, 1.8));
	const float disk = exp(-V_2 * 15.0);

	float flare = lensflare(v.xy, sunPos, 0.5 * sun_intensity, 0.0);
	flare = flare * flare + disk;
	color = light_color * flare;
	output.color.rgb = lerp(output.color.rgb, color, 0.8 * flare * sun_intensity);
	*/
	
	return output;
}
