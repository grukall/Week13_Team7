#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsInterface.h"

#include "Engine/Engine.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Tasks/TaskScheduler.h"
#include "UObject/UObjectHash.h"
#include "Physics/PhysicsFiltering.h"
#include <PxPhysicsAPI.h>

#include "PhysXVehicleManager.h"

class FPhysXCPUDispatcher final : public physx::PxCpuDispatcher
{
public:
	void submitTask(physx::PxBaseTask& Task) override
	{
		Tasks::FTaskScheduler::Get().Schedule({ &ExecuteTask, &Task });
	}

	physx::PxU32 getWorkerCount() const override
	{
		return Tasks::FTaskScheduler::Get().GetNumWorkers();
	}

private:
	static void ExecuteTask(void* UserData)
	{
		auto& Task = *static_cast<physx::PxBaseTask*>(UserData);
		Task.run();
		// release가 후속 task를 실행 가능하게 하므로 run이 끝난 뒤 호출한다.
		Task.release();
	}
};

namespace
{
	using namespace physx;

	PxFilterFlags PhysXSimFilterShader(PxFilterObjectAttributes, PxFilterData A, PxFilterObjectAttributes, PxFilterData B, PxPairFlags& Flags, const void*, PxU32)
	{
		const PxU32 ChannelA = static_cast<uint32>(GetCollisionChannel(A.word3));
		const PxU32 ChannelB = static_cast<uint32>(GetCollisionChannel(B.word3));
		if (!(A.word1 & (1u << ChannelB)) || !(B.word1 & (1u << ChannelA)))
			return PxFilterFlag::eSUPPRESS;
		Flags = PxPairFlag::eCONTACT_DEFAULT;
		if ((A.word3 | B.word3) & EPDF_CCD)
			Flags |= PxPairFlag::eDETECT_CCD_CONTACT;
		if ((A.word3 | B.word3) & EPDF_ContactNotify)
			Flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_CCD;
		return PxFilterFlag::eDEFAULT;
	}
}

class FPhysXSimEventCallback final : public physx::PxSimulationEventCallback
{
	struct FNotify
	{
		UPrimitiveComponent* Receiver;
		UPrimitiveComponent* Other;
		uint32 ReceiverId, OtherId;
		bool operator==(const FNotify&) const = default;
	};
	TArray<FNotify> Pending;

public:
	void onContact(const physx::PxContactPairHeader& Header, const physx::PxContactPair* Pairs, physx::PxU32 Count) override
	{
		using namespace physx;
		if (Header.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
			return;
		bool Notify = false;
		for (PxU32 Index = 0; Index < Count; ++Index)
			Notify |= bool(Pairs[Index].events & (PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_CCD));
		if (!Notify)
			return;
		for (int Index = 0; Index < 2; ++Index)
		{
			auto* Body = static_cast<FBodyInstance*>(Header.actors[Index]->userData);
			auto* Peer = static_cast<FBodyInstance*>(Header.actors[1 - Index]->userData);
			if (Body && Peer && Body->bNotifyRigidBodyCollision)
			{
				auto* A = Body->GetOwnerComponent();
				auto* B = Peer->GetOwnerComponent();
				Pending.AddUnique({A, B, A->GetUUID(), B->GetUUID()});
			}
		}
	}

	void DispatchPhysNotifications()
	{
		auto Notifications = std::move(Pending);
		for (const auto& Hit : Notifications)
		{
			uint32 Id;
			if (TryGetLiveObjectUUID(Hit.Receiver, nullptr, Id) && Id == Hit.ReceiverId &&
				TryGetLiveObjectUUID(Hit.Other, nullptr, Id) && Id == Hit.OtherId &&
				Hit.Receiver->IsRegistered() && Hit.Other->IsRegistered())
				Hit.Receiver->DispatchHit(Hit.Other);
		}
	}

	void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
	void onWake(physx::PxActor**, physx::PxU32) override {}
	void onSleep(physx::PxActor**, physx::PxU32) override {}
	void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
	void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
};

bool FPhysXRuntime::Init()
{
	// Foundation은 두 callback을 참조하므로 SDK 해제까지 유효해야 한다.
	static physx::PxDefaultAllocator Allocator;
	static physx::PxDefaultErrorCallback Errors;
	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, Errors);
	if (!Foundation)
		return false;

	Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, physx::PxTolerancesScale());
	if (!Physics)
		return false;
	Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, physx::PxCookingParams(Physics->getTolerancesScale()));
	if(!Cooking)
	{
		return false;
	}

	if(!physx::PxInitVehicleSDK(*Physics))
	{
		return false;
	}
	physx::PxVehicleSetBasisVectors(physx::PxVec3(0, 0, 1), physx::PxVec3(1, 0, 0)); // Up = +Z, Forward = +X
	physx::PxVehicleSetUpdateMode(physx::PxVehicleUpdateMode::eVELOCITY_CHANGE);
	bVehicleSDKInitialize = true;

	return true;
}

FPhysXRuntime::~FPhysXRuntime()
{
	if (Cooking)
		Cooking->release();
	if(bVehicleSDKInitialize)
		physx::PxCloseVehicleSDK();
	if (Physics)
		Physics->release();
	if (Foundation)
		Foundation->release();
}

bool UWorld::CreatePhysicsScene()
{
	auto NewScene = MakeUnique<FPhysScene>();
	if (!NewScene->Init(GEngine->GetPhysXRuntime()))
		return false;
	PhysicsScene = std::move(NewScene);
	return true;
}

bool FPhysScene::Init(const FPhysXRuntime& Runtime)
{
	physx::PxSceneDesc Desc(Runtime.Physics->getTolerancesScale());
	Desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, -9.81f);
	CPUDispatcher = new FPhysXCPUDispatcher();
	Desc.cpuDispatcher = CPUDispatcher;
	Desc.flags |= physx::PxSceneFlag::eENABLE_CCD;
	Desc.filterShader = PhysXSimFilterShader;
	SimEventCallback = new FPhysXSimEventCallback();
	Desc.simulationEventCallback = SimEventCallback;
	Scene = Runtime.Physics->createScene(Desc);

	if(Scene == nullptr)
	{
		return false;
	}
	
	VehicleManager = new FPhysXVehicleManager(Scene);

	return true;
}

void FPhysScene::AddActorsToScene_AssumesLocked(const TArray<FPhysicsActorHandle>& InActors)
{
	for (const FPhysicsActorHandle& Actor : InActors)
		Scene->addActor(*Actor.SyncActor);
}

FPhysScene::~FPhysScene()
{
	WaitPhysScenes();
	delete VehicleManager;
	VehicleManager = nullptr;
	if (Scene)
		Scene->release();
	delete SimEventCallback;
	delete CPUDispatcher;
}

void FPhysScene::StartFrame(float DeltaSeconds)
{
	if (!(DeltaSeconds > 0) || !std::isfinite(DeltaSeconds))
		return;
	constexpr float MaxSubstepDeltaTime = 1.0f / 60.0f;
	constexpr int MaxSubsteps = 6;
	const float FrameTime = std::min(DeltaSeconds, MaxSubsteps * MaxSubstepDeltaTime);
	const int Steps = int(std::ceil(FrameTime / MaxSubstepDeltaTime));

	struct FKinematicMove
	{
		physx::PxRigidDynamic* Actor;
		physx::PxTransform Start, Target;
	};
	TArray<FKinematicMove> Moves;
	for (const auto& [Body, TargetTM] : PendingKinematicTargets)
	{
		auto* Dynamic = Body->ActorHandle.SyncActor->is<physx::PxRigidDynamic>();
		const auto& P = TargetTM.Location;
		const auto& Q = TargetTM.Rotation;
		const physx::PxTransform Target({P.X, P.Y, P.Z}, {Q.X, Q.Y, Q.Z, Q.W});
		if (Dynamic && Target.isValid())
			Moves.Add({Dynamic, Dynamic->getGlobalPose(), Target});
	}
	PendingKinematicTargets.Reset();

	for (int Step = 1; Step <= Steps; ++Step)
	{
		const float Alpha = float(Step) / Steps;
		for (const auto& Move : Moves)
		{
			const auto& A = Move.Start;
			const auto& B = Move.Target;
			const auto Q = FMath::Slerp({A.q.x, A.q.y, A.q.z, A.q.w}, {B.q.x, B.q.y, B.q.z, B.q.w}, Alpha);
			const physx::PxQuat Rotation(Q.X, Q.Y, Q.Z, Q.W);
			Move.Actor->setKinematicTarget(physx::PxTransform(A.p + (B.p - A.p) * Alpha, Rotation));
		}

		if(VehicleManager)
		{
			VehicleManager->Update(FrameTime / Steps);
		}

		Scene->simulate(FrameTime / Steps);
		if (Step == Steps)
			bSimulating = true;
		else if (!FetchSubstepResults())
			return;
	}
}

bool FPhysScene::FetchSubstepResults()
{
	const bool bFetched = Scene->fetchResults(true);
	if (!bFetched)
	{
		HTR_LOG(Error, "PhysX fetchResults failed");
		return false;
	}
	physx::PxSceneReadLock ReadLock(*Scene);
	physx::PxU32 Count;
	auto** Actors = Scene->getActiveActors(Count);
	for (physx::PxU32 Index = 0; Index < Count; ++Index)
	{
		auto* Body = static_cast<FBodyInstance*>(Actors[Index]->userData);
		if (Body && Body->bSimulatePhysics)
			ActiveBodies.Add(Body);
	}
	return true;
}

void FPhysScene::ProcessPhysScene()
{
	if (!bSimulating)
		return;
	const bool bFetched = FetchSubstepResults();
	bSimulating = false;
	if (bFetched)
		for (auto* Body : ActiveBodies)
			Body->SyncComponentToRBPhysics();
	ActiveBodies.Reset();
}

void FPhysScene::WaitPhysScenes()
{
	// Scene을 변경하는 Game Thread 호출은 미완료 frame을 먼저 마무리한다.
	ProcessPhysScene();
}

void FPhysScene::EndFrame()
{
	SimEventCallback->DispatchPhysNotifications();
}
