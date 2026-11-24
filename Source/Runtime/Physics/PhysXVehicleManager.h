#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include <PxBatchQueryDesc.h>
#include <vehicle/PxVehicleUpdate.h>

class UWheeledVehicleMovementComponent;

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
	void AddVehicle(UWheeledVehicleMovementComponent* Comp, physx::PxVehicleWheels* Vehicle);
	void RemoveVehicle(physx::PxVehicleWheels* Vehicle);
	void RemoveVehicle(UWheeledVehicleMovementComponent* Comp, physx::PxVehicleWheels* Vehicle);

	void SetupBatchedSceneQuery();
	void UpdateTireFrictionTable();

	void Update(float SubstepDeltaTime);

	const physx::PxVehicleWheelQueryResult* GetWheelQueryResults(const physx::PxVehicleWheels* Vehicle) const;

private:
	physx::PxScene* Scene = nullptr;
	physx::PxBatchQuery* BatchQuery = nullptr;

	TArray<physx::PxVehicleWheels*> Vehicles;
	TArray<physx::PxRaycastQueryResult> RaycastQueryResults;
	TArray<physx::PxRaycastHit> RaycastHits;
	TArray<physx::PxVehicleWheelQueryResult> VehicleWheelQueryResults;
	TArray<physx::PxWheelQueryResult> WheelQueryResults;
	TArray< UWheeledVehicleMovementComponent*> VehicleComponents;

	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* SurfaceTirePairs = nullptr;

	bool bFrictionTableDirty = true;
	bool bBatchQueryDirty = true;
};