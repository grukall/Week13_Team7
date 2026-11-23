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

    // 설계값 (직렬화)
    FString ConstraintBone1;            // 자식
    FString ConstraintBone2;            // 부모
    FTransform Frame1;                  // 자식(Bone1) 입장에서 조인트 위치
    FTransform Frame2;                  // 부모(Bone2) 입장에서 조인트 위치
    EAngularMotion TwistMotion = EAngularMotion::Free;
    EAngularMotion Swing1Motion = EAngularMotion::Free;
    EAngularMotion Swing2Motion = EAngularMotion::Free;
    float TwistLimitDeg = 30, Swing1LimitDeg = 45, Swing2LimitDeg = 45;

    // 런타임
    physx::PxD6Joint* ConstraintHandle = nullptr;
    FPhysScene* PhysicsScene = nullptr;
    void InitConstraint(FBodyInstance* Child, FBodyInstance* Parent, FPhysScene* Scene);
    void TermConstraint();
};