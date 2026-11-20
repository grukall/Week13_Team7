#include "EnginePCH.h"
#include "Physics/PhysicsInterface.h"
#include "Physics/BodySetup.h"
#include "Engine/Engine.h"
#include <PxPhysicsAPI.h>

namespace
{
	using namespace physx;

	PxVec3 ToPx(const FVector& V) { return PxVec3(V.X, V.Y, V.Z); }
	PxQuat ToPx(const FQuat& Q) { return PxQuat(Q.X, Q.Y, Q.Z, Q.W); }

	template<class GeometryType>
	bool AttachShape(PxRigidActor& Actor, const GeometryType& Geometry, PxMaterial& Material, const PxTransform& LocalPose)
	{
		if (!Geometry.isValid() || !LocalPose.isValid())
			return false;
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(Actor, Geometry, Material);
		if (!Shape)
			return false;
		Shape->setLocalPose(LocalPose);
		return true;
	}

	bool AttachShapes(PxRigidActor& Actor, const FKAggregateGeom& Geom, const FVector& Scale, PxMaterial& Material)
	{
		const FVector Size = Scale.GetAbs();
		for (const FKBoxElem& Box : Geom.BoxElems)
		{
			const PxBoxGeometry Geometry(Box.X * Size.X * 0.5f, Box.Y * Size.Y * 0.5f, Box.Z * Size.Z * 0.5f);
			if (!AttachShape(Actor, Geometry, Material, PxTransform(ToPx(Box.Center * Scale), ToPx(Box.Rotation))))
				return false;
		}
		for (const FKSphereElem& Sphere : Geom.SphereElems)
		{
			const auto Scaled = Sphere.GetFinalScaled(Scale);
			const float Radius = std::max(Scaled.Radius, KINDA_SMALL_NUMBER * CmToWorld);
			if (!AttachShape(Actor, PxSphereGeometry(Radius), Material, PxTransform(ToPx(Scaled.Center))))
				return false;
		}
		for (const FKSphylElem& Capsule : Geom.SphylElems)
		{
			if (Capsule.Length < 0 || Capsule.Radius < 0 || !std::isfinite(Capsule.Length) || !std::isfinite(Capsule.Radius))
				return false;
			// UE preserves a capsule even when its two hemisphere centers nearly coincide.
			const auto Scaled = Capsule.GetFinalScaled(Scale);
			const PxCapsuleGeometry Geometry(Scaled.Radius, Scaled.Length * 0.5f);
			const PxTransform LocalPose(ToPx(Scaled.Center), ToPx(Scaled.Rotation) * PxQuat(-PxHalfPi, PxVec3(0, 1, 0)));
			if (!AttachShape(Actor, Geometry, Material, LocalPose))
				return false;
		}
		for (const FKConvexElem& Convex : Geom.ConvexElems)
		{
			// A convex mesh requires positive PxMeshScale. NegX carries the reflection;
			// the remaining even number of sign flips is a local half-turn.
			const bool bMirror = (Scale.X < 0) ^ (Scale.Y < 0) ^ (Scale.Z < 0);
			auto* Mesh = (bMirror ? Convex.ConvexMeshNegX : Convex.ConvexMesh).get();
			PxQuat Rotation(PxIdentity);
			if (Scale.Y < 0 && Scale.Z < 0)
				Rotation = PxQuat(1, 0, 0, 0);
			else if (Scale.Y < 0)
				Rotation = PxQuat(0, 0, 1, 0);
			else if (Scale.Z < 0)
				Rotation = PxQuat(0, 1, 0, 0);
			const PxConvexMeshGeometry Geometry(Mesh, PxMeshScale(ToPx(Size), PxQuat(PxIdentity)));
			if (!AttachShape(Actor, Geometry, Material, PxTransform(Rotation)))
				return false;
		}
		return true;
	}
}

void FPhysicsInterface::CreateActor(const FActorCreationParams& Params, FPhysicsActorHandle& Handle)
{
	using namespace physx;
	const FTransform& Transform = Params.InitialTM;
	const PxTransform Pose(ToPx(Transform.Location), ToPx(Transform.Rotation));
	if (!Pose.isValid() || !ToPx(Transform.Scale).isFinite())
		return;

	auto& Physics = *GEngine->GetPhysXRuntime().Physics;
	if (Params.bStatic)
		Handle.SyncActor = Physics.createRigidStatic(Pose);
	else
	{
		auto* Dynamic = Physics.createRigidDynamic(Pose);
		if (Dynamic)
			Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eUSE_KINEMATIC_TARGET_FOR_SCENE_QUERIES, true);
		Handle.SyncActor = Dynamic;
	}
}

void FPhysicsInterface::SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& Handle, bool bIsKinematic)
{
	if (auto* Dynamic = Handle.SyncActor->is<physx::PxRigidDynamic>())
		Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bIsKinematic);
}

bool FPhysicsInterface::AddGeometry(const FPhysicsActorHandle& Handle, const FGeometryAddParams& Params)
{
	return AttachShapes(*Handle.SyncActor, *Params.Geometry, Params.Scale, *GEngine->GetPhysXRuntime().DefaultMaterial);
}

bool FPhysicsInterface::IsInScene(const FPhysicsActorHandle& Handle)
{
	return Handle.IsValid() && Handle.SyncActor->getScene() != nullptr;
}

void FPhysicsInterface::ReleaseActor(FPhysicsActorHandle& Handle)
{
	if (Handle.SyncActor)
		Handle.SyncActor->release();
	Handle.SyncActor = nullptr;
}
