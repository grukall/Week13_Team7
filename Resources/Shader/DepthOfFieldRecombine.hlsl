#include "DepthOfFieldGatherCommon.hlsli"

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    return FullscreenTrianglePosition(vertexID);
}

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SignedCoC : register(t1);
Texture2D<float4> NearLayer : register(t2);
Texture2D<float4> FarLayer : register(t3);

SamplerState LinearClamp : register(s0);

float4 mainPS(float4 position : SV_Position) : SV_Target
{
    int3 coord = int3(position.xy, 0);
    float4 sharp = SceneColor.Load(coord);
    uint width, height;
    NearLayer.GetDimensions(width, height);
    float2 uv = position.xy / (2.0f * float2(width, height));
    float4 near = NearLayer.SampleLevel(LinearClamp, uv, 0);
    float4 far = FarLayer.SampleLevel(LinearClamp, uv, 0);
    float blur = saturate(abs(SignedCoC.Load(coord)) - 0.5f);
    float3 base = lerp(sharp.rgb, far.rgb / max(far.a, 1e-6f), blur * far.a);
    return float4(base * (1 - near.a) + near.rgb, sharp.a);
}
