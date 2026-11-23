#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include <PxBatchQueryDesc.h>
#include <vehicle/PxVehicleUpdate.h>

namespace physx
{
	class PxScene;
	class PxVehicleWheels;
	class PxBatchQuery;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
}

class FPhysXVehicleManager
{
public:
	explicit FPhysXVehicleManager(physx::PxScene* InScene);
	~FPhysXVehicleManager();

	FPhysXVehicleManager(const FPhysXVehicleManager&) = delete;
	FPhysXVehicleManager& operator=(const FPhysXVehicleManager&) = delete;

	void AddVehicle(physx::PxVehicleWheels* Vehicle);
	void RemoveVehicle(physx::PxVehicleWheels* Vehicle);

	void SetupBatchedSceneQuery();
	void UpdateTireFrictionTable();

	void Update(float SubstepDeltaTime);

private:
	physx::PxScene* Scene = nullptr;
	physx::PxBatchQuery* BatchQuery = nullptr;

	TArray<physx::PxVehicleWheels*> Vehicles;
	TArray<physx::PxRaycastQueryResult> RaycastQueryResults;
	TArray<physx::PxRaycastHit> RaycastHits;
	TArray<physx::PxVehicleWheelQueryResult> VehicleWheelQueryResults;
	TArray<physx::PxWheelQueryResult> WheelQueryResults;

	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* SurfaceTirePairs = nullptr;

	bool bFrictionTableDirty = true;
	bool bBatchQueryDirty = true;
};