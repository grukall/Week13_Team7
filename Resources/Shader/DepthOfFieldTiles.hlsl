#include "DepthOfFieldGatherCommon.hlsli"
Texture2D<float> SignedCoC : register(t0);

// A focused destination can still be covered by a nearby defocused foreground.
float mainPS(float4 position : SV_Position) : SV_Target
{
    int2 origin = int2(position.xy) * TileSize;
    uint width, height;
    SignedCoC.GetDimensions(width, height);
    int2 end = min(origin + TileSize, int2(width, height));
    float nearRadius = 0;
    for (int y = origin.y; y < end.y; ++y)
        for (int x = origin.x; x < end.x; ++x)
            nearRadius = max(nearRadius, -SignedCoC.Load(int3(x, y, 0)));
    return min(nearRadius, MaxGatherRadius);
}
