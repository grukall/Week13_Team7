#pragma once

#include "UObject/Class.h"
#include "Physics/PhysicsInterface.h"
#include "BodyInstance.generated.h"

class UPrimitiveComponent;
class UBodySetup;
class FPhysScene;

UENUM()
enum class ECollisionChannel : uint8
{
	ECC_WorldStatic,
	ECC_WorldDynamic,
	ECC_Pawn,
	ECC_Visibility,
	ECC_Camera,
	ECC_PhysicsBody,
	ECC_Vehicle,
	ECC_Destructible
};

UENUM()
enum class ECollisionResponse : uint8
{
	ECR_Ignore,
	ECR_Overlap,
	ECR_Block
};

UENUM()
enum class ECollisionEnabled : uint8
{
	NoCollision,
	QueryOnly,
	PhysicsOnly,
	QueryAndPhysics
};

USTRUCT()
struct FCollisionResponseContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldStatic = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldDynamic = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Pawn = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Visibility = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Camera = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse PhysicsBody = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Vehicle = ECollisionResponse::ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Destructible = ECollisionResponse::ECR_Block;
	ECollisionResponse GetResponse(ECollisionChannel Channel) const;
	void SetResponse(ECollisionChannel Channel, ECollisionResponse Response);
};

USTRUCT()
struct FBodyInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;

	UPROPERTY(EditAnywhere)
	ECollisionChannel ObjectType = ECollisionChannel::ECC_WorldDynamic;
	UPROPERTY(EditAnywhere)
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	UPROPERTY(EditAnywhere)
	FCollisionResponseContainer CollisionResponses;
	UPROPERTY(EditAnywhere)
	bool bUseCCD = false;
	UPROPERTY(EditAnywhere)
	bool bNotifyRigidBodyCollision = false;

	void UpdatePhysicsFilterData();
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }
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
