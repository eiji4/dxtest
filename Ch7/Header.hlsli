struct Output
{
	float4 svpos : SV_POSITION;
	float4 normal : NORMAL;
	float2 uv : TEXCOORD;
	min16uint2 boneno : BONE_NO;
	min16uint weight : WEIGHT;
};

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

cbuffer cbuff0 : register(b0)
{
	matrix world;
	matrix viewproj;
}