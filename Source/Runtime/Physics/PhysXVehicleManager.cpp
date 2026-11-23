#include "EnginePCH.h"
#include "Physics/PhysXVehicleManager.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicalMaterial.h"

#include "Components/WheeledVehicleMovementComponent.h"

#include "Engine/Engine.h"

#include <PxPhysicsAPI.h>

namespace
{
	physx::PxQueryHitType::Enum PreFilterShader(
		physx::PxFilterData FilterData0,
		physx::PxFilterData FilterData1,
		const void* ConstantBlock,
		physx::PxU32 ConstantBlockSize,
		physx::PxHitFlags& QueryFlags)
	{
		PX_UNUSED(ConstantBlock);
		PX_UNUSED(ConstantBlockSize);
		PX_UNUSED(QueryFlags);

		if (FilterData0.word0 != 0 && FilterData0.word0 == FilterData1.word0)
		{
			return physx::PxQueryHitType::eNONE;
		}

		if ((FilterData1.word1 & (1u << static_cast<uint32>(ECollisionChannel::ECC_Vehicle))) == 0)
		{
			return physx::PxQueryHitType::eNONE;
		}

		return physx::PxQueryHitType::eBLOCK;
	}
}

FPhysXVehicleManager::FPhysXVehicleManager(physx::PxScene* InScene)
	: Scene(InScene)
{
}

FPhysXVehicleManager::~FPhysXVehicleManager()
{
	if (BatchQuery)
	{
		BatchQuery->release();
		BatchQuery = nullptr;
	}
	if (SurfaceTirePairs)
	{
		SurfaceTirePairs->release();
		SurfaceTirePairs = nullptr;
	}
}

void FPhysXVehicleManager::AddVehicle(physx::PxVehicleWheels* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}

	Vehicles.AddUnique(Vehicle);
	bBatchQueryDirty = true;
}

void FPhysXVehicleManager::AddVehicle(UWheeledVehicleMovementComponent* Comp, physx::PxVehicleWheels* Vehicle)
{
	AddVehicle(Vehicle);

	if(!Comp)
	{
		return;
	}

	VehicleComponents.AddUnique(Comp);
	bBatchQueryDirty = true;
}

void FPhysXVehicleManager::RemoveVehicle(physx::PxVehicleWheels* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}

	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		if (Vehicles[i] == Vehicle)
		{
			Vehicles.RemoveAtSwap(static_cast<uint32>(i));
			bBatchQueryDirty = true;
			return;
		}
	}
}

void FPhysXVehicleManager::RemoveVehicle(UWheeledVehicleMovementComponent* Comp, physx::PxVehicleWheels* Vehicle)
{
	RemoveVehicle(Vehicle);

	if (!Comp)
	{
		return;
	}

	for (int32 i = 0; i < VehicleComponents.Num(); ++i)
	{
		if (VehicleComponents[i] == Comp)
		{
			VehicleComponents.RemoveAtSwap(static_cast<uint32>(i));
			bBatchQueryDirty = true;
			return;
		}
	}
}

void FPhysXVehicleManager::SetupBatchedSceneQuery()
{
	if (BatchQuery)
	{
		BatchQuery->release();
		BatchQuery = nullptr;
	}

	bBatchQueryDirty = false;

	if (!Scene || Vehicles.IsEmpty())
	{
		RaycastQueryResults.Reset();
		RaycastHits.Reset();
		WheelQueryResults.Reset();
		VehicleWheelQueryResults.Reset();
		return;
	}

	uint32 TotalWheels = 0;
	for (physx::PxVehicleWheels* Vehicle : Vehicles)
	{
		if (!Vehicle)
		{
			continue;
		}

		TotalWheels += Vehicle->mWheelsSimData.getNbWheels();
	}

	if (TotalWheels == 0)
	{
		return;
	}

	RaycastQueryResults.SetNum(static_cast<int32>(TotalWheels));
	RaycastHits.SetNum(static_cast<int32>(TotalWheels));
	WheelQueryResults.SetNum(static_cast<int32>(TotalWheels));

	VehicleWheelQueryResults.SetNum(Vehicles.Num());
	uint32 Offset = 0;
	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		const uint32 NbWheels = Vehicles[i] ? Vehicles[i]->mWheelsSimData.getNbWheels() : 0;
		VehicleWheelQueryResults[i].wheelQueryResults = (NbWheels > 0) ? &WheelQueryResults[static_cast<int32>(Offset)] : nullptr;
		VehicleWheelQueryResults[i].nbWheelQueryResults = NbWheels;
		Offset += NbWheels;
	}

	physx::PxBatchQueryDesc Desc(TotalWheels, 0, 0);
	Desc.queryMemory.userRaycastResultBuffer = RaycastQueryResults.GetData();
	Desc.queryMemory.userRaycastTouchBuffer = RaycastHits.GetData();
	Desc.queryMemory.raycastTouchBufferSize = TotalWheels;
	Desc.preFilterShader = PreFilterShader;

	BatchQuery = Scene->createBatchQuery(Desc);
}

void FPhysXVehicleManager::UpdateTireFrictionTable()
{
	if (SurfaceTirePairs)
	{
		SurfaceTirePairs->release();
		SurfaceTirePairs = nullptr;
	}

	if (!GEngine || !GEngine->DefaultPhysMaterial)
	{
		return;
	}

	physx::PxMaterial* DefaultPhysMaterial = GEngine->DefaultPhysMaterial->GetPhysicsMaterial().Material;
	if (!DefaultPhysMaterial)
	{
		return;
	}

	const physx::PxMaterial* SurfaceMaterials[1];
	SurfaceMaterials[0] = DefaultPhysMaterial;

	physx::PxVehicleDrivableSurfaceType SurfaceTypes[1];
	SurfaceTypes[0].mType = 0;

	SurfaceTirePairs = physx::PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	if (!SurfaceTirePairs)
	{
		return;
	}

	SurfaceTirePairs->setup(1, 1, SurfaceMaterials, SurfaceTypes);
	SurfaceTirePairs->setTypePairFriction(0, 0, 1.0f);
	bFrictionTableDirty = false;
}

void FPhysXVehicleManager::Update(float SubstepDeltaTime)
{
	if (!Scene || Vehicles.IsEmpty() || SubstepDeltaTime <= 0.0f)
	{
		return;
	}

	if (bFrictionTableDirty || !SurfaceTirePairs)
	{
		UpdateTireFrictionTable();
		if (!SurfaceTirePairs)
		{
			return;
		}
	}

	if (bBatchQueryDirty || !BatchQuery)
	{
		SetupBatchedSceneQuery();
		if (!BatchQuery)
		{
			return;
		}
	}

	for(UWheeledVehicleMovementComponent* Comp : VehicleComponents)
	{
		if(Comp)
		{
			Comp->PreTick(SubstepDeltaTime);
		}
	}

	physx::PxVehicleSuspensionRaycasts(
		BatchQuery,
		static_cast<physx::PxU32>(Vehicles.Num()),
		Vehicles.GetData(),
		static_cast<physx::PxU32>(RaycastQueryResults.Num()),
		RaycastQueryResults.GetData());

	physx::PxVehicleUpdates(
		SubstepDeltaTime,
		Scene->getGravity(),
		*SurfaceTirePairs,
		static_cast<physx::PxU32>(Vehicles.Num()),
		Vehicles.GetData(),
		VehicleWheelQueryResults.GetData());
}

const physx::PxVehicleWheelQueryResult* FPhysXVehicleManager::GetWheelQueryResults(const physx::PxVehicleWheels* Vehicle) const
{
	if (!Vehicle || bBatchQueryDirty)
	{
		return nullptr;
	}

	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		if (Vehicles[i] == Vehicle)
		{
			return VehicleWheelQueryResults.IsValidIndex(i) ? &VehicleWheelQueryResults[i] : nullptr;
		}
	}

	return nullptr;
}
