#pragma once

#include "UObject/Class.h"
#include "Physics/PhysicsInterface.h"
#include "BodyInstance.generated.h"

class UPrimitiveComponent;
class UBodySetup;
class FPhysScene;

USTRUCT()
struct FBodyInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;

	// Only configuration is serialized. Each registered World gets its own runtime actor.
	FPhysicsActorHandle ActorHandle;
	void InitBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* PrimComp, FPhysScene* InRBScene);
	bool IsValidBodyInstance() const { return ActorHandle.IsValid(); }
	void TermBody();
	void SetInstanceSimulatePhysics(bool bSimulate);
	void SetBodyTransform(const FTransform& Transform);
	void SyncComponentToRBPhysics();

private:
	friend class FPhysScene;
	UPrimitiveComponent* OwnerComponent = nullptr;
	FPhysScene* PhysicsScene = nullptr;
	bool bSyncingComponent = false;
};
