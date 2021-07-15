float4 BasicPS(float4 pos: SV_POSITION) : SV_TARGET
{
	//return float4((float2(0,1) + pos.xy) * 0.5f, 1.0f, 1.0f);
	return float4(0.0f, 0.0f, 1.0f, 1.0f);
}