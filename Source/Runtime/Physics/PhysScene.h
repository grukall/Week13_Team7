#pragma once

#include "Core/Types.h"

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
}

class FPhysXRuntime;
class FPhysXCPUDispatcher;

class FPhysScene
{
public:
	FPhysScene() = default;
	bool Init(const FPhysXRuntime& Runtime);
	~FPhysScene();
	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

private:
	physx::PxScene* Scene = nullptr;
	FPhysXCPUDispatcher* CPUDispatcher = nullptr;
};

// Engine 소유. 맵 전환 중 공존하는 World들도 같은 SDK를 사용한다.
class FPhysXRuntime
{
public:
	FPhysXRuntime() = default;
	~FPhysXRuntime();
	FPhysXRuntime(const FPhysXRuntime&) = delete;
	FPhysXRuntime& operator=(const FPhysXRuntime&) = delete;

	bool Init();

private:
	friend class FPhysScene;
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
};
