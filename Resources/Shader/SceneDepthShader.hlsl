#include "DepthOfFieldCommon.hlsli"

cbuffer SceneDepthConstants : register(b0)
{
    float A;
    float B;
    float MaxRange;
    uint bIsPerspective;
    float FocusDistanceMeters;
    float InfinityRadiusPixels;
    uint bVisualizeDOFRadius;
    float Padding;
};

Texture2D<float> SceneDepthTexture : register(t0);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    static const float2 Positions[6] =
    {
        float2(-1, 1),
        float2(1, 1),
        float2(-1, -1),

        float2(-1, -1),
        float2(1, 1),
        float2(1, -1)
    };

    VSOutput Output;
    Output.Position = float4(Positions[VertexID], 0, 1);
    return Output;
}

float ConvertFromDeviceZ(float DeviceZ)
{
    if (bIsPerspective != 0)
    {
        return B / (DeviceZ - A);
    }

    return (DeviceZ - B) / A;
}

float4 mainPS(VSOutput Input) : SV_Target
{
    int2 PixelPosition = int2(Input.Position.xy);
    float DeviceZ = SceneDepthTexture.Load(int3(PixelPosition, 0));

    if (DeviceZ >= 1.0f && bVisualizeDOFRadius == 0)
    {
        return float4(0, 0, 0, 0);
    }

    float ViewDepth = ConvertFromDeviceZ(DeviceZ);
    if (bVisualizeDOFRadius != 0)
    {
        float radiusPx = SignedCoCRadius(DeviceZ, A, B, FocusDistanceMeters, InfinityRadiusPixels);
        float3 tint = radiusPx < 0.0f ? float3(0.1f, 0.4f, 1.0f) : float3(1.0f, 0.45f, 0.05f);
        return float4(tint * saturate(abs(radiusPx) / 8.0f), 1.0f);
    }
    float Grey = saturate(ViewDepth / MaxRange);

    return float4(Grey, Grey, Grey, 1);
}
