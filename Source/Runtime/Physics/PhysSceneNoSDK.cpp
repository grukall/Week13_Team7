#include "EnginePCH.h"
#include "Engine/Engine.h"

// Tool configurations have no SDK resources to initialize or release.
bool FPhysXRuntime::Init()
{
	return true;
}

FPhysXRuntime::~FPhysXRuntime() = default;

bool UWorld::CreatePhysicsScene()
{
	return true;
}

FPhysScene::~FPhysScene() = default;
