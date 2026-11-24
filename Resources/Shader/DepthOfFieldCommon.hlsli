// Perspective depth is A + B/z. Clear depth represents the sky at infinity.
float SignedCoCRadius(float deviceZ, float A, float B, float focusDistance, float infinityRadius)
{
    float inverseDepth = deviceZ >= 1.0f ? 0.0f : (deviceZ - A) / B;
    return infinityRadius * (1.0f - focusDistance * inverseDepth);
}
