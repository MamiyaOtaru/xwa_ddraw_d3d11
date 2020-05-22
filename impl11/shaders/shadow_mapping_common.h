// ShadowMapVertexShaderMatrixCB (the same struct is used for both the vertex and pixel shader)
cbuffer ConstantBuffer : register(b5)
{
	matrix lightViewProj;
	matrix lightWorldMatrix;
	uint sm_enabled, sm_debug, sm_unused0, sm_unused1;
	float sm_aspect_ratio, sm_bias, sm_max_edge_distance, sm_unused2;
};

