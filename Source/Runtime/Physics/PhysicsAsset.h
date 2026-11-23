#pragma once

#include <Core/Types.h>
#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "SkeletalBodySetup.h"
#include "PhysicsConstraintTemplate.h"
#include "PhysicsAsset.generated.h"

class USkeletalMesh;

//Bone별 SkeletalBodySetup, 관절(joint) 설계도
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()

public:
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	int32 FindBodyIndex(const FString &BoneName) const;
	void UpdateBodySetupIndexMap();
	static UPhysicsAsset* CreateFromSkeletalMesh(const USkeletalMesh* Mesh);

	TArray<USkeletalBodySetup*> SkeletalBodySetups;
	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;
	TMap<FString, int32> BodySetupIndexMap;
};