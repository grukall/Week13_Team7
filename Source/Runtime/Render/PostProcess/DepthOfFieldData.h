#pragma once

struct alignas(16) FDepthOfFieldData
{
	float ProjectionA;
	float ProjectionB;
	float FocusDistance;
	float InfinityRadius;
};

static_assert(sizeof(FDepthOfFieldData) == 16);
