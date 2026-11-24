#include "DepthOfFieldGatherCommon.hlsli"
Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float> TileNearRadius : register(t2);

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 mainPS(float4 position : SV_Position) : SV_Target
{
    int2 pixel = int2(position.xy);
    float4 sharp = SceneColor.Load(int3(pixel, 0));
    float centerRadius = GatherRadius(SceneDepth.Load(int3(pixel, 0)));
    uint width, height;
    SceneDepth.GetDimensions(width, height);
    int2 lastPixel = int2(width, height) - 1;

    // MaxGatherRadius is one tile wide. Only these tiles can scatter onto this pixel.
    int2 firstTile = uint2(max(pixel - MaxGatherRadius, 0)) / uint(TileSize);
    int2 lastTile = uint2(min(pixel + MaxGatherRadius, lastPixel)) / uint(TileSize);
    float searchRadius = max(0, centerRadius);
    for (int ty = firstTile.y; ty <= lastTile.y; ++ty)
        for (int tx = firstTile.x; tx <= lastTile.x; ++tx)
            searchRadius = max(searchRadius, TileNearRadius.Load(int3(tx, ty, 0)));
    if (searchRadius <= 0.5f)
        return sharp;

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
            float radius = GatherRadius(SceneDepth.Load(int3(samplePixel, 0)));
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

    float3 background = farWeight > 0 ? farColor / farWeight : sharp.rgb;
    float3 foreground = nearColor / max(coverage, 1e-6f);
    return float4(lerp(background, foreground, saturate(coverage)), sharp.a);
}
