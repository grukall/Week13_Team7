#include "EnginePCH.h"
#include "Physics/PhysScene.h"

#include "Engine/Engine.h"
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
	return Physics != nullptr;
}

FPhysXRuntime::~FPhysXRuntime()
{
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
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, -9.81f);
	CPUDispatcher = new FPhysXCPUDispatcher();
	Desc.cpuDispatcher = CPUDispatcher;
	Desc.filterShader = physx::PxDefaultSimulationFilterShader;
	Scene = Runtime.Physics->createScene(Desc);
	return Scene != nullptr;
}

FPhysScene::~FPhysScene()
{
	if (Scene)
		Scene->release();
	delete CPUDispatcher;
}
