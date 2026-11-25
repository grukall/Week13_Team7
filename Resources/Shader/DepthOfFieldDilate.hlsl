#include "DepthOfFieldGatherCommon.hlsli"

float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    return FullscreenTrianglePosition(vertexID);
}

Texture2D<float4> FlattenedTiles : register(t0);

float4 mainPS(float4 position : SV_Position) : SV_Target
{
    int2 tile = int2(position.xy);
    uint width, height;
    FlattenedTiles.GetDimensions(width, height);
    int tileReach = (MaxGatherRadius + TileSize - 1) / TileSize;
    int2 first = max(tile - tileReach, 0);
    int2 last = min(tile + tileReach, int2(width, height) - 1);
    float4 bounds = FlattenedTiles.Load(int3(tile, 0));
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x)
        {
            int2 offset = abs(int2(x, y) - tile);
            float distance = length(float2(max((offset - 1) * TileSize + 1, 0)));
            float4 neighbor = FlattenedTiles.Load(int3(x, y, 0));
            if (-neighbor.x + 0.5f > distance)
                bounds.xy = float2(min(bounds.x, neighbor.x), max(bounds.y, neighbor.y));
            if (neighbor.w + 0.5f > distance)
                bounds.zw = float2(min(bounds.z, neighbor.z), max(bounds.w, neighbor.w));
        }
    return bounds;
}
