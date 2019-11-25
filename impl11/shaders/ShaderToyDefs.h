#define vec4 float4
#define vec3 float3
#define vec2 float2

#define bvec2 bool2
#define bvec3 bool3
#define bvec4 bool4

#define mat4 float4x4

#define mix lerp
#define fract frac

#define mod(x, y) (x % y)

#define PI 3.14159265
#define ATAN5 1.37340076695
#define TAU 6.28318

cbuffer ConstantBuffer : register(b7)
{
	float3 iMouse;
	float  iTime;
	// 16 bytes
	float2 iResolution;
	float disk_size, unused2;
	// 32 bytes
	float x0, y0, x1, y1; // Limits in uv-coords of the viewport
}

