#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/PhysScene.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsFiltering.h"
#include <PxPhysicsAPI.h>

namespace
{
	constexpr ECollisionResponse FCollisionResponseContainer::* ResponseFields[] = {
		&FCollisionResponseContainer::WorldStatic,
		&FCollisionResponseContainer::WorldDynamic,
		&FCollisionResponseContainer::Pawn,
		&FCollisionResponseContainer::Visibility,
		&FCollisionResponseContainer::Camera,
		&FCollisionResponseContainer::PhysicsBody,
		&FCollisionResponseContainer::Vehicle,
		&FCollisionResponseContainer::Destructible
	};
}

ECollisionResponse FCollisionResponseContainer::GetResponse(ECollisionChannel Channel) const
{
	return this->*ResponseFields[static_cast<uint8>(Channel)];
}

void FCollisionResponseContainer::SetResponse(ECollisionChannel Channel, ECollisionResponse Response)
{
	this->*ResponseFields[static_cast<uint8>(Channel)] = Response;
}

void FBodyInstance::UpdatePhysicsFilterData()
{
	using namespace physx;
	if (!ActorHandle.IsValid())
		return;
	if (PhysicsScene)
		PhysicsScene->WaitPhysScenes();
	const auto* Owner = OwnerComponent->GetOwner();
	const auto Enabled = Owner && !Owner->GetActorEnableCollision() ? ECollisionEnabled::NoCollision : CollisionEnabled;
	const bool Query = Enabled == ECollisionEnabled::QueryOnly || Enabled == ECollisionEnabled::QueryAndPhysics;
	const bool Sim = Enabled == ECollisionEnabled::PhysicsOnly || Enabled == ECollisionEnabled::QueryAndPhysics;
	PxFilterData QueryData;
	QueryData.word0 = Owner ? Owner->GetUUID() : 0;
	QueryData.word3 = static_cast<uint32>(ObjectType) << NumFilterDataFlagBits;
	for (uint32 Index = 0; Index < std::size(ResponseFields); ++Index)
	{
		auto Response = CollisionResponses.GetResponse(static_cast<ECollisionChannel>(Index));
		if (Response == ECollisionResponse::ECR_Block)
			QueryData.word1 |= 1u << Index;
		else if (Response == ECollisionResponse::ECR_Overlap)
			QueryData.word2 |= 1u << Index;
	}
	PxFilterData SimData(0, QueryData.word1, OwnerComponent->GetUUID(), QueryData.word3);
	if (bUseCCD)
		SimData.word3 |= EPDF_CCD;
	if (bNotifyRigidBodyCollision)
		SimData.word3 |= EPDF_ContactNotify;
	auto* Actor = ActorHandle.SyncActor;
	for (PxU32 Index = 0; Index < Actor->getNbShapes(); ++Index)
	{
		PxShape* Shape;
		Actor->getShapes(&Shape, 1, Index);
		Shape->setQueryFilterData(QueryData);
		Shape->setSimulationFilterData(SimData);
		Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, Query);
		Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, Sim);
	}
	Actor->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, !Sim);
	if (!Sim && PhysicsScene)
		PhysicsScene->PendingKinematicTargets.Remove(this);
	if (auto* Dynamic = Actor->is<PxRigidDynamic>())
		Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, bUseCCD && bSimulatePhysics);
}

void FBodyInstance::InitBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* PrimComp, FPhysScene* InRBScene)
{
	if (IsValidBodyInstance() || !Setup || !PrimComp || !InRBScene)
		return;

	InRBScene->WaitPhysScenes();
	FActorCreationParams Params;
	Params.InitialTM = Transform;
	Params.bStatic = PrimComp->Mobility != EComponentMobility::Movable;
	FPhysicsInterface::CreateActor(Params, ActorHandle);
	if (!IsValidBodyInstance())
		return;
	if (!Params.bStatic)
		FPhysicsInterface::SetIsKinematic_AssumesLocked(ActorHandle, !bSimulatePhysics);

	if (!Setup->AddShapesToRigidActor_AssumesLocked(this, Transform.Scale))
	{
		TermBody();
		return;
	}
	if (!Params.bStatic && !physx::PxRigidBodyExt::updateMassAndInertia(*ActorHandle.SyncActor->is<physx::PxRigidDynamic>(), 1.0f))
	{
		TermBody();
		return;
	}

	OwnerComponent = PrimComp;
	UpdatePhysicsFilterData();
	InRBScene->AddActorsToScene_AssumesLocked({ ActorHandle });
	if (!FPhysicsInterface::IsInScene(ActorHandle))
	{
		TermBody();
		return;
	}
	PhysicsScene = InRBScene;
	ActorHandle.SyncActor->userData = this;
}

void FBodyInstance::TermBody()
{
	if (PhysicsScene)
	{
		PhysicsScene->WaitPhysScenes();
		PhysicsScene->ActiveBodies.Remove(this);
		PhysicsScene->PendingKinematicTargets.Remove(this);
	}
	FPhysicsInterface::ReleaseActor(ActorHandle);
	OwnerComponent = nullptr;
	PhysicsScene = nullptr;
}

void FBodyInstance::SetInstanceSimulatePhysics(bool bSimulate)
{
	if (!PhysicsScene)
	{
		bSimulatePhysics = bSimulate;
		return;
	}
	auto* Dynamic = ActorHandle.SyncActor->is<physx::PxRigidDynamic>();
	if (!Dynamic || bSimulatePhysics == bSimulate)
		return;

	PhysicsScene->WaitPhysScenes();
	if (!bSimulate)
		SyncComponentToRBPhysics();
	if (bSimulate && OwnerComponent->GetAttachParent())
		OwnerComponent->DetachFromParent(EAttachmentRule::KeepWorld);

	bSimulatePhysics = bSimulate;
	FPhysicsInterface::SetIsKinematic_AssumesLocked(ActorHandle, !bSimulate);
	UpdatePhysicsFilterData();
	if (bSimulate)
	{
		if (const auto* Target = PhysicsScene->PendingKinematicTargets.Find(this))
		{
			SetBodyTransform(*Target);
			PhysicsScene->PendingKinematicTargets.Remove(this);
		}
		if (!Dynamic->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
			Dynamic->wakeUp();
	}
}

void FBodyInstance::SetBodyTransform(const FTransform& Transform)
{
	if (bSyncingComponent || !PhysicsScene)
		return;
	const auto& P = Transform.Location;
	const auto& Q = Transform.Rotation;
	const physx::PxTransform Pose({P.X, P.Y, P.Z}, {Q.X, Q.Y, Q.Z, Q.W});
	if (!Pose.isValid())
		return;

	bSyncingComponent = true;
	PhysicsScene->WaitPhysScenes();
	bSyncingComponent = false;
	auto* Actor = ActorHandle.SyncActor;
	if (auto* Dynamic = Actor->is<physx::PxRigidDynamic>(); Dynamic && !bSimulatePhysics && !Actor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
	{
		// Queries see the new target immediately; StartFrame interpolates it for simulation.
		Dynamic->setKinematicTarget(Pose);
		PhysicsScene->PendingKinematicTargets.Add(this, Transform);
	}
	else
		Actor->setGlobalPose(Pose);
}

void FBodyInstance::SyncComponentToRBPhysics()
{
	if (bSyncingComponent || !bSimulatePhysics || OwnerComponent->GetBodyInstance() != this)
		return;
	const auto Pose = ActorHandle.SyncActor->getGlobalPose();
	const FVector Location(Pose.p.x, Pose.p.y, Pose.p.z);
	const FQuat Rotation(Pose.q.x, Pose.q.y, Pose.q.z, Pose.q.w);
	bSyncingComponent = true;
	OwnerComponent->SetWorldLocationAndRotation(Location, Rotation.ToFRotator(), false);
	bSyncingComponent = false;
}
