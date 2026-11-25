#include "DepthOfFieldGatherCommon.hlsli"
Texture2D<float> SignedCoC : register(t0);

// RG: signed foreground min/max. BA: signed background min/max.
float4 mainPS(float4 position : SV_Position) : SV_Target
{
    int2 origin = int2(position.xy) * TileSize;
    uint width, height;
    SignedCoC.GetDimensions(width, height);
    int2 end = min(origin + TileSize, int2(width, height));
    float4 bounds = float4(0, -MaxGatherRadius, MaxGatherRadius, 0);
    for (int y = origin.y; y < end.y; ++y)
        for (int x = origin.x; x < end.x; ++x)
        {
            float radius = clamp(SignedCoC.Load(int3(x, y, 0)), -MaxGatherRadius, MaxGatherRadius);
            bounds.x = min(bounds.x, radius);
            bounds.y = max(bounds.y, min(0, radius));
            bounds.z = min(bounds.z, max(0, radius));
            bounds.w = max(bounds.w, radius);
        }
    return bounds;
}
