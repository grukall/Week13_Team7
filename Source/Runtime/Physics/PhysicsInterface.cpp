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
	// PxTransform은 위치 + 회전만 가진다. Scale은 강체/조인트 프레임에 의미가 없으므로 버린다.
	// 회전은 정규화해서 넘긴다 (PxTransform::isValid가 단위 쿼터니언을 요구).
	PxTransform ToPx(const FTransform& T) { return PxTransform(ToPx(T.Location), ToPx(T.Rotation).getNormalized()); }

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

FPhysicsMaterialHandle FPhysicsInterface::CreateMaterial(UPhysicalMaterial* Material)
{
	if (!GEngine || !GEngine->GetPhysXRuntime().Physics)
		return {};
	const float Friction = std::max(0.f, Material->Friction);
	auto* SDKMaterial = GEngine->GetPhysXRuntime().Physics->createMaterial(Friction, Friction, std::clamp(Material->Restitution, 0.f, 1.f));
	if (SDKMaterial)
		SDKMaterial->userData = Material;
	return {SDKMaterial};
}

void FPhysicsInterface::UpdateMaterial(const FPhysicsMaterialHandle& Handle, const UPhysicalMaterial& Material)
{
	if (!Handle.IsValid())
		return;
	using Mode = physx::PxCombineMode;
	const float Friction = std::max(0.f, Material.Friction);
	Handle.Material->setStaticFriction(Friction);
	Handle.Material->setDynamicFriction(Friction);
	Handle.Material->setRestitution(std::clamp(Material.Restitution, 0.f, 1.f));
	Handle.Material->setFrictionCombineMode(Material.bOverrideFrictionCombineMode ? static_cast<Mode::Enum>(Material.FrictionCombineMode) : Mode::eAVERAGE);
	Handle.Material->setRestitutionCombineMode(Material.bOverrideRestitutionCombineMode ? static_cast<Mode::Enum>(Material.RestitutionCombineMode) : Mode::eAVERAGE);
}

void FPhysicsInterface::ReleaseMaterial(FPhysicsMaterialHandle& Handle)
{
	if (Handle.IsValid())
	{
		Handle.Material->userData = nullptr;
		Handle.Material->release();
		Handle.Material = nullptr;
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
	auto* Material = Params.SimpleMaterial->GetPhysicsMaterial().Material;
	return Material && AttachShapes(*Handle.SyncActor, *Params.Geometry, Params.Scale, *Material);
}

bool FPhysicsInterface::IsInScene(const FPhysicsActorHandle& Handle)
{
	return Handle.IsValid() && Handle.SyncActor->getScene() != nullptr;
}

physx::PxD6Joint* FPhysicsInterface::CreateD6Joint(const FPhysicsActorHandle& Parent, const FTransform& ParentFrame,
                                                   const FPhysicsActorHandle& Child, const FTransform& ChildFrame)
{
	using namespace physx;
	if (!GEngine || !GEngine->GetPhysXRuntime().Physics || !Parent.IsValid() || !Child.IsValid())
		return nullptr;
	const PxTransform Frame0 = ToPx(ParentFrame);
	const PxTransform Frame1 = ToPx(ChildFrame);
	if (!Frame0.isValid() || !Frame1.isValid())
		return nullptr;

	// actor0 = 부모 + 부모 기준 프레임, actor1 = 자식 + 자식 기준 프레임 (짝을 맞춰야 한다)
	auto& Physics = *GEngine->GetPhysXRuntime().Physics;
	return PxD6JointCreate(Physics, Parent.SyncActor, Frame0, Child.SyncActor, Frame1);
}

void FPhysicsInterface::ReleaseJoint(physx::PxD6Joint*& Joint)
{
	if (Joint)
		Joint->release();
	Joint = nullptr;
}

void FPhysicsInterface::ReleaseActor(FPhysicsActorHandle& Handle)
{
	if (Handle.SyncActor)
		Handle.SyncActor->release();
	Handle.SyncActor = nullptr;
}
