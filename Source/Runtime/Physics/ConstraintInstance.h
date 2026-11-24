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

    // Soft Limit: 제한각에 딱 막히지 않고 스프링처럼 밀려났다 돌아온다 (Stiffness = 밀어내는 힘, Damping = 튕김을 줄이는 힘)
    UPROPERTY(EditAnywhere)
    bool bSoftSwingLimit = false;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=1))
    float SwingStiffness = 50;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1))
    float SwingDamping = 5;
    UPROPERTY(EditAnywhere)
    bool bSoftTwistLimit = false;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=1))
    float TwistStiffness = 50;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1))
    float TwistDamping = 5;

    // 켜면 관절로 이어진 두 바디끼리는 충돌하지 않는다 (겹쳐 있는 이웃 캡슐이 서로 밀어내 튀는 걸 막는다)
    UPROPERTY(EditAnywhere)
    bool bDisableCollision = true;
    // 켜면 자식이 부모를 끌고 가지 못한다 (관절 계산에서 부모를 무한 질량으로 본다)
    UPROPERTY(EditAnywhere)
    bool bParentDominates = false;

    // Projection: 솔버가 못 지킨 오차가 허용치를 넘으면 자식 바디를 관절 위치로 강제로 되돌린다
    UPROPERTY(EditAnywhere)
    bool bEnableProjection = true;
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.001))
    float ProjectionLinearTolerance = 0.05f;     // 미터 (5cm 이상 벌어지면 붙인다)
    UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=180, Delta=0.5))
    float ProjectionAngularToleranceDeg = 180;   // 180 = 회전은 사실상 되돌리지 않음

    // 런타임
    physx::PxD6Joint* ConstraintHandle = nullptr;
    FPhysScene* PhysicsScene = nullptr;
    void InitConstraint(FBodyInstance* Child, FBodyInstance* Parent, FPhysScene* Scene);
    void TermConstraint();
};