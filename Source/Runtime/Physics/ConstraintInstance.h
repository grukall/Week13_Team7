#pragma once
#include "Core/EngineString.h"
#include <Math/Transform.h>
#include "ConstraintInstance.generated.h"


class FPhysScene;
struct FBodyInstance;
namespace physx { class PxD6Joint; }

UENUM()
enum class EAngularMotion : uint8
{
    Free,
    Limited,
    Locked
};

USTRUCT()
struct FConstraintInstance
{
    GENERATED_BODY()

    // 설계값 (직렬화는 UPhysicsConstraintTemplate::Serialize가 직접 한다).
    // UPROPERTY는 Physics Asset 디테일 패널에 그리기 위한 것. Frame1/Frame2는 패널이 직접 그린다 (FQuat 리플렉션 없음)
    UPROPERTY(VisibleAnywhere)
    FString ConstraintBone1;            // 자식
    UPROPERTY(VisibleAnywhere)
    FString ConstraintBone2;            // 부모
    FTransform Frame1;                  // 자식(Bone1) 입장에서 조인트 위치
    FTransform Frame2;                  // 부모(Bone2) 입장에서 조인트 위치
    UPROPERTY(EditAnywhere)
    EAngularMotion Swing1Motion = EAngularMotion::Limited;
    UPROPERTY(EditAnywhere)
    EAngularMotion Swing2Motion = EAngularMotion::Limited;
    UPROPERTY(EditAnywhere)
    EAngularMotion TwistMotion = EAngularMotion::Limited;
    // 자동 생성(CreateFromSkeletalMesh)도 이 기본값을 그대로 쓴다. 기본값은 여기 한 곳에서만 바꾼다.
    // PhysX 스윙 콘은 π 미만이어야 하고, 0이면 Locked와 구별이 안 돼 불안정하다
    UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=179, Delta=0.5))
    float Swing1LimitDeg = 35;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=179, Delta=0.5))
    float Swing2LimitDeg = 35;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=179, Delta=0.5))
    float TwistLimitDeg = 20;

    // 런타임
    physx::PxD6Joint* ConstraintHandle = nullptr;
    FPhysScene* PhysicsScene = nullptr;
    void InitConstraint(FBodyInstance* Child, FBodyInstance* Parent, FPhysScene* Scene);
    void TermConstraint();
};