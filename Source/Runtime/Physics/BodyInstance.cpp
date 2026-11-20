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
		TermBody();
}

void FBodyInstance::TermBody()
{
	FPhysicsInterface::ReleaseActor(ActorHandle);
}
