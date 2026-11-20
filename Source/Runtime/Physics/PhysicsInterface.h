#pragma once

#include "Math/Transform.h"

struct FKAggregateGeom;
namespace physx { class PxRigidActor; class PxGeometry; class PxTransform; }
class UWorld;
class UPrimitiveComponent;
struct FHitResult;
template<typename T> class TArray;

struct FPhysicsActorHandle
{
	physx::PxRigidActor* SyncActor = nullptr;
	bool IsValid() const { return SyncActor != nullptr; }
};

struct FActorCreationParams
{
	FTransform InitialTM;
	bool bStatic = false;
};

struct FGeometryAddParams
{
	const FKAggregateGeom* Geometry = nullptr;
	FVector Scale = FVector::OneVector;
};

class FPhysicsInterface
{
public:
	static void CreateActor(const FActorCreationParams& Params, FPhysicsActorHandle& Handle);
	static void SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& Handle, bool bIsKinematic);
	static bool AddGeometry(const FPhysicsActorHandle& Handle, const FGeometryAddParams& Params);
	static void GeomSweepMulti(const UWorld& World, const physx::PxGeometry& Geometry, const physx::PxTransform& ShapePose, const UPrimitiveComponent& Source, const FVector& Start, const FVector& End, float Length, TArray<FHitResult>& Hits);
	static bool IsInScene(const FPhysicsActorHandle& Handle);
	static void ReleaseActor(FPhysicsActorHandle& Handle);
};
