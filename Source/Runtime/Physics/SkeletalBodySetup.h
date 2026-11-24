#pragma once

#include "BodySetup.h"
#include "BodyInstance.h"
#include "Core/EngineString.h"
#include "SkeletalBodySetup.generated.h"

// 바디마다 래그돌에서 어떻게 움직일지 (UE의 EPhysicsType과 같다)
UENUM()
enum class EPhysicsType : uint8
{
	Default,     // 컴포넌트를 따른다 (Ragdoll ON이면 시뮬레이션, OFF면 애니메이션)
	Kinematic,   // 항상 애니메이션을 따른다 (예: pelvis만 고정하고 팔다리만 늘어뜨리기)
	Simulated    // 항상 시뮬레이션한다 (예: 걷는 애니메이션 중에 꼬리 · 포니테일만 흔들리기)
};

//UPhysicsAsset이 소유하는 BodySetup
// 아래 UPROPERTY는 리플렉션("Properties")으로 저장되고 Physics Asset 디테일 패널에 그대로 그려진다.
// 런타임 FBodyInstance에는 USkeletalMeshComponent::InitArticulated가 복사해 넣는다
UCLASS()
class USkeletalBodySetup : public UBodySetup
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere)
	FString BoneName;

	UPROPERTY(EditAnywhere)
	EPhysicsType PhysicsType = EPhysicsType::Default;

	// 끄면 질량 = 도형 부피 × PhysMaterial 밀도. 켜면 MassInKg를 그대로 쓴다
	UPROPERTY(EditAnywhere)
	bool bOverrideMass = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, Delta=0.1))
	float MassInKg = 10.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float LinearDamping = 0.05f;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float AngularDamping = 0.1f;

	UPROPERTY(EditAnywhere)
	bool bEnableGravity = true;

	// NoCollision이면 다른 바디·월드와 부딪히지 않는다 (관절로는 계속 이어진다)
	UPROPERTY(EditAnywhere)
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};
