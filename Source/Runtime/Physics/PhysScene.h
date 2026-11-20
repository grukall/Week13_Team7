#pragma once

#include "Core/Types.h"

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxMaterial;
	class PxCooking;
	class PxScene;
}

struct FPhysicsActorHandle;
template<typename T> class TArray;

class FPhysXRuntime;
class FPhysXCPUDispatcher;

class FPhysScene
{
public:
	FPhysScene() = default;
	bool Init(const FPhysXRuntime& Runtime);
	~FPhysScene();
	void AddActorsToScene_AssumesLocked(const TArray<FPhysicsActorHandle>& InActors);
	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

private:
	friend class FPhysicsInterface;
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
	friend class UBodySetup;
	friend class FPhysicsInterface;
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxCooking* Cooking = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
};
