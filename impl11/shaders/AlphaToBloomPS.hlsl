// Copyright (c) 2020 Leo Reyes
// Licensed under the MIT license. See LICENSE.txt
// Special shader for handling textures with alpha. This shader does the following:
// The default behavior converts the alpha of the color into bloom.
// If SPECIAL_CONTROL_NO_COLOR_ALPHA is set, then all the output will be
// zeroed out -- except for the bloom. This has the effect of producing a
// glowing, burry output, which looks good for energy fields, like the energy
// field in DSReactorCylinder.opt
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

PixelShaderOutput main(PixelShaderInput input)
{
	PixelShaderOutput output;
	float4 texelColor = texture0.Sample(sampler0, input.tex);
	float  alpha = texelColor.a;

	output.bloom = float4(fBloomStrength * texelColor.rgb, alpha);
	output.normal = 0;
	output.pos3D = 0;

	if (special_control == SPECIAL_CONTROL_NO_COLOR_ALPHA) {
		output.color = 0;
		output.ssMask = 0;
		output.ssaoMask = 0;
		return output;
	}

	output.color = texelColor;
	// ssaoMask.r: Material
	// ssaoMask.g: Glossiness
	// ssaoMask.b: Specular Intensity
	output.ssaoMask = float4(0.0, 0.0, 0.0, alpha);
	// SS Mask: Normal Mapping Intensity, Specular Value, Shadeless
	output.ssMask = float4(0.0, 0.0, 0.0, alpha);
	
	return output;
}
