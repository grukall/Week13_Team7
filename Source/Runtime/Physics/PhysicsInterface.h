#pragma once

#include "Math/Transform.h"

struct FKAggregateGeom;
class UPhysicalMaterial;
namespace physx { class PxMaterial; class PxRigidActor; class PxGeometry; class PxTransform; class PxD6Joint; }
class UWorld;
class UPrimitiveComponent;
struct FHitResult;
template<typename T> class TArray;

struct FPhysicsMaterialHandle
{
	physx::PxMaterial* Material = nullptr;
	bool IsValid() const { return Material != nullptr; }
};

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
	UPhysicalMaterial* SimpleMaterial = nullptr;
	FVector Scale = FVector::OneVector;
};

class FPhysicsInterface
{
public:
	static FPhysicsMaterialHandle CreateMaterial(UPhysicalMaterial* Material);
	static void UpdateMaterial(const FPhysicsMaterialHandle& Handle, const UPhysicalMaterial& Material);
	static void ReleaseMaterial(FPhysicsMaterialHandle& Handle);
	static void CreateActor(const FActorCreationParams& Params, FPhysicsActorHandle& Handle);
	static void SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& Handle, bool bIsKinematic);
	static bool AddGeometry(const FPhysicsActorHandle& Handle, const FGeometryAddParams& Params);
	static void GeomSweepMulti(const UWorld& World, const physx::PxGeometry& Geometry, const physx::PxTransform& ShapePose, const UPrimitiveComponent& Source, const FVector& Start, const FVector& End, float Length, TArray<FHitResult>& Hits);
	static bool IsInScene(const FPhysicsActorHandle& Handle);
	static void ReleaseActor(FPhysicsActorHandle& Handle);

	// 두 강체를 D6 조인트로 연결. 각 Frame은 해당 강체(=본) 로컬 공간 기준 조인트 위치/축.
	// 호출 측에서 WaitPhysScenes() 후 호출할 것.
	static physx::PxD6Joint* CreateD6Joint(const FPhysicsActorHandle& Parent, const FTransform& ParentFrame,
	                                       const FPhysicsActorHandle& Child, const FTransform& ChildFrame);
	static void ReleaseJoint(physx::PxD6Joint*& Joint);
};
