#include "DepthOfFieldCommon.hlsli"

cbuffer LensConstants : register(b0)
{
    float ProjectionA;
    float ProjectionB;
    float FocusDistance;
    float InfinityRadius;
};

Texture2D<float> SceneDepth : register(t0);

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float mainPS(float4 position : SV_Position) : SV_Target
{
    float deviceZ = SceneDepth.Load(int3(int2(position.xy), 0));
    return SignedCoCRadius(deviceZ, ProjectionA, ProjectionB, FocusDistance, InfinityRadius);
}
