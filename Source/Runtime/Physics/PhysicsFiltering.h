#pragma once

#include "Physics/BodyInstance.h"

// UE 4.27 packs six extra mask bits and five channel bits above the flags.
constexpr uint32 NumFilterDataFlagBits = 21;
constexpr uint32 EPDF_CCD = 0x0004;
constexpr uint32 EPDF_ContactNotify = 0x0008;

inline ECollisionChannel GetCollisionChannel(uint32 Word3)
{
	return static_cast<ECollisionChannel>((Word3 >> NumFilterDataFlagBits) & 31);
}
