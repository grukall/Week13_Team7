#pragma once

// Lens settings carried by a camera view. Focus is in engine meters; filmback is in millimeters.
struct FDepthOfFieldSettings
{
	float FocusDistanceMeters = 5.0f;
	float FStop = 2.8f;
	float SensorWidthMM = 36.0f;
};
