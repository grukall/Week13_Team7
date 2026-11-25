float4 mainVS(uint vertexID : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

static const int TileSize = 8;
// Setup stores the optical radius; limit only the gather footprint.
static const int MaxGatherRadius = 8;
