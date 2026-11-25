#include "DepthOfFieldGatherCommon.hlsli"
Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SignedCoC : register(t1);
Texture2D<float> TileNearRadius : register(t2);

struct Layers
{
    float4 nearLayer : SV_Target0;
    float4 farLayer : SV_Target1;
};

Layers mainPS(float4 position : SV_Position)
{
    int2 pixel = int2(position.xy);
    Layers layers = (Layers)0;
    float centerRadius = clamp(SignedCoC.Load(int3(pixel, 0)), -MaxGatherRadius, MaxGatherRadius);
    uint width, height;
    SignedCoC.GetDimensions(width, height);
    int2 lastPixel = int2(width, height) - 1;

    // MaxGatherRadius is one tile wide. Only these tiles can scatter onto this pixel.
    int2 firstTile = uint2(max(pixel - MaxGatherRadius, 0)) / uint(TileSize);
    int2 lastTile = uint2(min(pixel + MaxGatherRadius, lastPixel)) / uint(TileSize);
    float searchRadius = max(0, centerRadius);
    for (int ty = firstTile.y; ty <= lastTile.y; ++ty)
        for (int tx = firstTile.x; tx <= lastTile.x; ++tx)
            searchRadius = max(searchRadius, TileNearRadius.Load(int3(tx, ty, 0)));
    if (searchRadius <= 0.5f)
        return layers;

    float3 farColor = 0, nearColor = 0;
    float farWeight = 0, coverage = 0;
    int extent = (int)ceil(searchRadius);
    for (int y = -extent; y <= extent; ++y)
    {
        for (int x = -extent; x <= extent; ++x)
        {
            float distancePx = length(float2(x, y));
            if (distancePx >= searchRadius + 0.5f)
                continue;
            int2 samplePixel = clamp(pixel + int2(x, y), 0, lastPixel);
            float radius = clamp(SignedCoC.Load(int3(samplePixel, 0)), -MaxGatherRadius, MaxGatherRadius);
            float3 color = SceneColor.Load(int3(samplePixel, 0)).rgb;
            if (radius < -0.5f)
            {
                // Each foreground sample spreads its energy over its own disc.
                float disc = saturate(-radius + 0.5f - distancePx);
                float weight = disc / max(1.0f, 3.14159f * radius * radius);
                nearColor += color * weight;
                coverage += weight;
            }
            else
            {
                // For a near center, adjacent visible background can fill the uncovered fraction.
                float support = centerRadius < -0.5f ? -centerRadius : min(max(0, radius), max(0, centerRadius));
                float weight = saturate(support + 0.5f - distancePx);
                farColor += color * weight;
                farWeight += weight;
            }
        }
    }

    layers.nearLayer = float4(nearColor / max(coverage, 1e-6f) * saturate(coverage), saturate(coverage));
    if (farWeight > 0)
        layers.farLayer = float4(farColor / farWeight, 1);
    return layers;
}
