#include "DepthOfFieldCommon.hlsli"

cbuffer LensConstants : register(b0)
{
    float ProjectionA;
    float ProjectionB;
    float FocusDistance;
    float InfinityRadius;
};

static const int TileSize = 8;
// Limit the gather support, while the diagnostic view keeps the physical radius.
static const int MaxGatherRadius = 8;

float GatherRadius(float depth)
{
    return clamp(SignedCoCRadius(depth, ProjectionA, ProjectionB, FocusDistance, InfinityRadius),
                 -MaxGatherRadius, MaxGatherRadius);
}
