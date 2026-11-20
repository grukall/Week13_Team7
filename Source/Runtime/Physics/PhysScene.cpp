#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsInterface.h"

#include "Engine/Engine.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Tasks/TaskScheduler.h"
#include <PxPhysicsAPI.h>

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
	if (!Cooking)
		return false;
	DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.0f);
	return DefaultMaterial != nullptr;
}

FPhysXRuntime::~FPhysXRuntime()
{
	if (DefaultMaterial)
		DefaultMaterial->release();
	if (Cooking)
		Cooking->release();
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
	Desc.filterShader = physx::PxDefaultSimulationFilterShader;
	Scene = Runtime.Physics->createScene(Desc);
	return Scene != nullptr;
}

void FPhysScene::AddActorsToScene_AssumesLocked(const TArray<FPhysicsActorHandle>& InActors)
{
	for (const FPhysicsActorHandle& Actor : InActors)
		Scene->addActor(*Actor.SyncActor);
}

FPhysScene::~FPhysScene()
{
	WaitPhysScenes();
	if (Scene)
		Scene->release();
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
	// 후속 collision notification을 전달할 시점. Pose는 ProcessPhysScene에서 반영한다.
}
