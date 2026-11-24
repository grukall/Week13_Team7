#pragma once

#include <Core/Types.h>
#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "SkeletalBodySetup.h"
#include "PhysicsConstraintTemplate.h"
#include "PhysicsAsset.generated.h"

class USkeletalMesh;

// 자동 생성에 쓰는 도형 (UE EPhysAssetFitGeomType). Box · Sphere는 다른 프리미티브 지원 작업 때 추가한다
enum class EPhysAssetFitGeomType : uint8
{
	Capsule
};

// 정점을 어느 본의 바디 크기에 넣을지 (UE EPhysAssetFitVertWeight)
enum class EPhysAssetFitVertWeight : uint8
{
	AnyWeight,        // 가중치가 0보다 큰 모든 본에 넣는다 (이웃 본과 공유하는 정점까지 들어가 바디가 크고 서로 겹친다)
	DominantWeight    // 가중치가 가장 큰 본 하나에만 넣는다 (UE 기본값. 바디가 본에 딱 맞는다)
};

// 바디 · 관절 자동 생성 옵션 (UE FPhysAssetCreateParams 중 우리 생성기에 맞는 것만). 단위는 미터.
// 기준값은 Dominant로 UAL1_Standard를 재서 정했다 (캡슐 전체 길이 = 길이 + 지름):
//   hand 0.119 · ball 0.112 → 남김 / thumb_01 0.104 · 손가락 0.026~0.067 → 뺌 (22 바디)
struct FPhysAssetCreateParams
{
	float MinBoneSize = 0.11f;          // 캡슐 전체 길이가 이보다 짧은 본은 바디를 만들지 않는다
	float MinBoneRadius = 0.02f;        // 반지름이 이보다 가는 본도 만들지 않는다 (가늘고 긴 본. 손가락 반지름은 0.010~0.016)
	EPhysAssetFitGeomType GeomType = EPhysAssetFitGeomType::Capsule;
	EPhysAssetFitVertWeight VertWeight = EPhysAssetFitVertWeight::DominantWeight;   // UE 기본값과 같다
	bool bBodyForAll = false;           // 크기 기준을 무시하고 정점이 있는 모든 본에 바디를 만든다
	bool bCreateConstraints = true;     // 바디가 있는 가장 가까운 조상과 관절로 잇는다
	EAngularMotion AngularConstraintMode = EAngularMotion::Limited;   // 만든 관절의 Swing1 · Swing2 · Twist Motion
};

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
	static UPhysicsAsset* CreateFromSkeletalMesh(const USkeletalMesh* Mesh, const FPhysAssetCreateParams& Params = FPhysAssetCreateParams());
	// 자동 생성(CreateFromSkeletalMesh)과 같은 관절 프레임. 에디터의 "기본값으로 스냅"이 쓴다.
	// 자식 프레임 축은 캡슐 길이 축이라, 생성할 때와 같은 VertWeight를 넘겨야 같은 값이 나온다
	static bool CalcDefaultConstraintFrames(const USkeletalMesh* Mesh, const FString& ChildBone, const FString& ParentBone,
		FTransform& OutFrame1, FTransform& OutFrame2,
		EPhysAssetFitVertWeight VertWeight = EPhysAssetFitVertWeight::DominantWeight);

	TArray<USkeletalBodySetup*> SkeletalBodySetups;
	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;
	TMap<FString, int32> BodySetupIndexMap;
};