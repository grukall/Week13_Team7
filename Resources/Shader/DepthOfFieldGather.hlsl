#include "DepthOfFieldGatherCommon.hlsli"

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    return FullscreenTrianglePosition(vertexID);
}

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SignedCoC : register(t1);
Texture2D<float4> DilatedTiles : register(t2);

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

    float4 bounds = DilatedTiles.Load(int3(uint2(pixel) / uint(TileSize), 0));
    float searchRadius = max(-bounds.x, bounds.w);
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
            else if (radius > 0.5f || centerRadius < -0.5f)
            {
                // Behind a near center, nearby visible background can fill uncovered area.
                float support = centerRadius < -0.5f ? max(radius, -centerRadius) : radius;
                float weight = saturate(support + 0.5f - distancePx) / max(1.0f, support * support);
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
