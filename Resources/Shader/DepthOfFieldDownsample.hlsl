#include "DepthOfFieldGatherCommon.hlsli"

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    return FullscreenTrianglePosition(vertexID);
}

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SignedCoC : register(t1);

struct HalfResolutionInput
{
    float4 color : SV_Target0;
    float radius : SV_Target1;
};

HalfResolutionInput mainPS(float4 position : SV_Position)
{
    uint width, height;
    SignedCoC.GetDimensions(width, height);
    int2 origin = int2(position.xy) * 2;
    int2 end = min(origin + 2, int2(width, height));
    float3 sum = 0;
    float minRadius = SignedCoC.Load(int3(origin, 0));
    for (int y = origin.y; y < end.y; ++y)
        for (int x = origin.x; x < end.x; ++x)
        {
            sum += SceneColor.Load(int3(x, y, 0)).rgb;
            minRadius = min(minRadius, SignedCoC.Load(int3(x, y, 0)));
        }
    HalfResolutionInput result;
    result.color = float4(sum / ((end.x - origin.x) * (end.y - origin.y)), 1);
    // Gather distances and radii must both be in half-resolution texels.
    result.radius = 0.5f * minRadius;
    return result;
}
