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
	// 자동 생성(CreateFromSkeletalMesh)과 같은 관절 프레임. 에디터의 "기본값으로 스냅"이 쓴다
	static bool CalcDefaultConstraintFrames(const USkeletalMesh* Mesh, const FString& ChildBone, const FString& ParentBone,
		FTransform& OutFrame1, FTransform& OutFrame2);

	TArray<USkeletalBodySetup*> SkeletalBodySetups;
	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;
	TMap<FString, int32> BodySetupIndexMap;
};