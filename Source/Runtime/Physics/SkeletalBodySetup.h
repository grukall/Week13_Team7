#pragma once

#include "BodySetup.h"
#include "Core/EngineString.h"
#include "SkeletalBodySetup.generated.h"

//UPhysicsAsset이 소유하는 BodySetup
UCLASS()
class USkeletalBodySetup : public UBodySetup
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString BoneName;
};