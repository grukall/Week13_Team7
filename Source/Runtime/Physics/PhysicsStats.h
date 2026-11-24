#pragma once
#include "Core/Stats/LightweightStats.h"

// stat physics의 CPU 시간 항목. 개수(바디 · 관절 수)는 오버레이가 컴포넌트를 돌며 센다.
namespace PhysicsStats
{
	// 월드 물리 한 프레임: StartFrame(시뮬레이션 시작) ~ ProcessPhysScene(결과 받기) ~ EndFrame
	DECLARE_CYCLE_STAT("Physics: Simulate", STAT_PhysicsSimulate);
	// 래그돌: 애니메이션 포즈 → Kinematic 바디 (PrePhysics)
	DECLARE_CYCLE_STAT("Physics: Update Kinematic", STAT_PhysicsUpdateKinematic);
	// 래그돌: 시뮬레이션 바디 → 본 (PostPhysics)
	DECLARE_CYCLE_STAT("Physics: Blend In Physics", STAT_PhysicsBlendIn);
}
