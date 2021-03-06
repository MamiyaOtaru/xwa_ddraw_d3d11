// Copyright (c) 2014 J�r�my Ansel
// Licensed under the MIT license. See LICENSE.txt
// Extended for SSAO by Leo Reyes, 2019
#include "shader_common.h"

cbuffer ConstantBuffer : register(b0)
{
	float4 vpScale;
	float aspect_ratio, cockpit_threshold, z_override, sz_override;
	float mult_z_override, bPreventTransform, bFullTransform;
};

/*
cbuffer ConstantBuffer : register(b1)
{
	matrix projEyeMatrix;
	matrix viewMatrix;
	matrix fullViewMatrix;
};
*/

struct VertexShaderInput
{
	float4 pos      : POSITION;
	float4 color    : COLOR0;
	float4 specular : COLOR1;
	float2 tex      : TEXCOORD;
};

struct PixelShaderInput
{
	float4 pos    : SV_POSITION;
	float4 color  : COLOR0;
	float2 tex    : TEXCOORD0;
	float4 pos3D  : COLOR1;
	float4 normal : NORMAL;
};

/*
PixelShaderInput main(VertexShaderInput input)
{
	PixelShaderInput output;

	output.pos.x = (input.pos.x * vpScale.x - 1.0f) * vpScale.z;
	output.pos.y = (input.pos.y * vpScale.y + 1.0f) * vpScale.z;
	output.pos.z = input.pos.z;
	output.pos.w = 1.0f;
	output.pos *= 1.0f / input.pos.w;

	output.color = input.color.zyxw;
	output.tex = input.tex;
	return output;
}
*/

PixelShaderInput main(VertexShaderInput input)
{
	PixelShaderInput output;
	float w = 1.0 / input.pos.w;
	float3 temp = input.pos.xyz;

	// Regular Vertex Shader
	output.pos.xy = (input.pos.xy * vpScale.xy + float2(-1.0, 1.0)) * vpScale.z;
	output.pos.z = input.pos.z;
	output.pos.w = 1.0f;

	// DirectX divides by output.pos.w internally. We don't see that division; but it happens.
	// Normally, this division by w accomplishes the final 3D -> 2D projection; but we're not
	// using a projection matrix here and we're providing final 2D coordinates, so we multiply
	// *everything* (output.pos) by w to cancel the internal division that DirectX will do to
	// keep our original coordinates above.
	output.pos   *= w;
	output.color  = input.color.zyxw;
	output.tex    = input.tex;
	// This line requires hook_normals.dll:
	output.normal = input.specular;
	// Use the following line when not using the normals hook:
	//output.normal = 0;

	// Back-project into 3D space (this is necessary to compute the normal map and enable effects like AO):
	// Normalize into the -1..1 range
	temp.xy *= vpScale.xy;
	temp.xy += float2(-1.0, 1.0); 
	// We use (-1.0, 1.0) above to place the center of the screen at the origin because 
	// the code adds a factor of 2 in Execute() for the non-VR case.

	// Apply the scale in 2D coordinates before back-projecting. This is
	// either g_fGlobalScale or g_fGUIElemScale (used to zoom-out the HUD
	// so that it's readable)
	temp.xy *= vpScale.z * float2(aspect_ratio, 1);
	temp.z = METRIC_SCALE_FACTOR * w; // This value was determined empirically
	// temp.z = w; // This setting provides a really nice depth for distant objects; but the cockpit is messed up
	// Override the depth of this element if z_override is set
	if (mult_z_override > -0.1)
		temp.z *= mult_z_override;
	if (z_override > -0.1)
		temp.z = z_override;

	// The back-projection into 3D is now very simple:
	float3 P = float3(temp.z * temp.xy, temp.z);
	// Write the reconstructed 3D into the output so that it gets interpolated:
	output.pos3D = float4(P, 1);

	return output;
}


