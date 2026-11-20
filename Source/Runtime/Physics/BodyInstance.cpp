#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/PhysScene.h"
#include <PxPhysicsAPI.h>

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

	InRBScene->AddActorsToScene_AssumesLocked({ ActorHandle });
	if (!FPhysicsInterface::IsInScene(ActorHandle))
	{
		TermBody();
		return;
	}
	PhysicsScene = InRBScene;
	OwnerComponent = PrimComp;
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
	if (bSimulate)
	{
		if (const auto* Target = PhysicsScene->PendingKinematicTargets.Find(this))
		{
			SetBodyTransform(*Target);
			PhysicsScene->PendingKinematicTargets.Remove(this);
		}
		Dynamic->wakeUp();
	}
}

void FBodyInstance::SetBodyTransform(const FTransform& Transform)
{
	if (bSyncingComponent || !PhysicsScene)
		return;
	if (!bSimulatePhysics && ActorHandle.SyncActor->is<physx::PxRigidDynamic>())
	{
		PhysicsScene->PendingKinematicTargets.Add(this, Transform);
		return;
	}
	// 완료 대기 중에도 방금 Component에서 지정한 pose를 보존한다.
	bSyncingComponent = true;
	PhysicsScene->WaitPhysScenes();
	bSyncingComponent = false;
	const auto& P = Transform.Location;
	const auto& Q = Transform.Rotation;
	const physx::PxTransform Pose({P.X, P.Y, P.Z}, {Q.X, Q.Y, Q.Z, Q.W});
	if (Pose.isValid())
		ActorHandle.SyncActor->setGlobalPose(Pose);
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
