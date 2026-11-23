#pragma once

#include "Core/Types.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "Math/Transform.h"

struct FBodyInstance;

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
class FPhysXSimEventCallback;
class FPhysXVehicleManager;

class FPhysScene
{
public:
	FPhysScene() = default;
	bool Init(const FPhysXRuntime& Runtime);
	~FPhysScene();
	void StartFrame(float DeltaSeconds);
	void ProcessPhysScene();
	void EndFrame();
	void WaitPhysScenes();
	void AddActorsToScene_AssumesLocked(const TArray<FPhysicsActorHandle>& InActors);
	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

	FPhysXVehicleManager* GetVehicleManager() { return VehicleManager; }

private:
	friend class FPhysicsInterface;
	friend struct FBodyInstance;
	friend class UWorld;
	FPhysXSimEventCallback* SimEventCallback = nullptr;
	bool FetchSubstepResults();
	TSet<FBodyInstance*> ActiveBodies;
	TMap<FBodyInstance*, FTransform> PendingKinematicTargets;
	bool bSimulating = false;
	physx::PxScene* Scene = nullptr;
	FPhysXCPUDispatcher* CPUDispatcher = nullptr;	

	FPhysXVehicleManager* VehicleManager = nullptr;
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
	friend class UWheeledVehicleMovementComponent;
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxCooking* Cooking = nullptr;

	bool bVehicleSDKInitialize = false;
};

