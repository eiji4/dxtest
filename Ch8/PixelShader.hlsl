#include "Header.hlsli"


float4 BasicPS(Output input) : SV_TARGET
{
	float3 light = normalize(float3(1, -1, 1));
	float3 reflectedLight = normalize(reflect(light, input.normal.xyz));
	float specIntensity = pow(saturate(dot(reflectedLight, -input.ray)), specular.a);
    
	float brightness = dot(-light, input.normal); // diffuseB in the book
	float2 normalUV = (input.normal.xy + float2(1, -1)) * float2(0.5, -0.5);
	float2 sphereMapUV = input.vnormal.xy;
    sphereMapUV = (sphereMapUV + float2(1, -1)) * float2(0.5, -0.5);
	float4 texColor = tex.Sample(smp, input.uv);
    float4 spec = float4(specIntensity * specular.rgb, 1);
    float4 amb = float4(texColor * ambient, 1);
    float4 blend =
          brightness
		* diffuse
		* texColor
		* sph.Sample(smp, sphereMapUV)		
		+ spa.Sample(smp, sphereMapUV) * texColor
		+ spec;


    //return spa.Sample(smp, sphereMapUV);
    //return float4(brightness, brightness, brightness, 1) * diffuse * texColor * sph.Sample(smp, sphereMapUV) + spa.Sample(smp, sphereMapUV) + spec;
    //return blend;
    return max(blend, amb);


}