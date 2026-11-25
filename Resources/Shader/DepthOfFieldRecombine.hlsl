#include "DepthOfFieldGatherCommon.hlsli"
Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SignedCoC : register(t1);
Texture2D<float4> NearLayer : register(t2);
Texture2D<float4> FarLayer : register(t3);

float4 mainPS(float4 position : SV_Position) : SV_Target
{
    int3 coord = int3(position.xy, 0);
    float4 sharp = SceneColor.Load(coord);
    float4 near = NearLayer.Load(coord);
    float4 far = FarLayer.Load(coord);
    float blur = saturate(abs(SignedCoC.Load(coord)) - 0.5f);
    float3 base = lerp(sharp.rgb, far.rgb / max(far.a, 1e-6f), blur * far.a);
    return float4(base * (1 - near.a) + near.rgb, sharp.a);
}
