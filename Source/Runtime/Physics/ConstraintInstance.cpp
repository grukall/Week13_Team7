#include "EnginePCH.h"
#include "BodyInstance.h"
#include "PhysScene.h"
#include "PhysicsInterface.h"
#include "ConstraintInstance.h"

#include <extensions/PxD6Joint.h>
#include <extensions/PxJointLimit.h>
#include <PxConstraint.h>

namespace
{
    physx::PxD6Motion::Enum MotionToPx(EAngularMotion M)
    {
        switch (M)
        {
        case EAngularMotion::Free:    return physx::PxD6Motion::eFREE;
        case EAngularMotion::Limited: return physx::PxD6Motion::eLIMITED;
        default:                      return physx::PxD6Motion::eLOCKED;
        }
    }
}

// 이미 만들어진 강체 두 개를 PhysX D6 관절로 묶는다.
void FConstraintInstance::InitConstraint(FBodyInstance* Child, FBodyInstance* Parent, FPhysScene* Scene)
{
    using namespace physx;
    if (ConstraintHandle || !Scene || !Child || !Parent ||
        !Child->IsValidBodyInstance() || !Parent->IsValidBodyInstance())
        return;

    Scene->WaitPhysScenes();

    PxD6Joint* Joint = FPhysicsInterface::CreateD6Joint(Parent->ActorHandle, Frame2, Child->ActorHandle, Frame1);
    if (!Joint)
        return;

    Joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
    Joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
    Joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);

    Joint->setMotion(PxD6Axis::eTWIST, MotionToPx(TwistMotion));
    Joint->setMotion(PxD6Axis::eSWING1, MotionToPx(Swing1Motion));
    Joint->setMotion(PxD6Axis::eSWING2, MotionToPx(Swing2Motion));

    // Soft면 PxSpring을 넣은 생성자를 쓴다. 제한각을 넘으면 Stiffness로 밀어내고 Damping으로 튕김을 줄인다
    const float Twist = FMath::DegreesToRadians(TwistLimitDeg);
    const float Swing1 = FMath::DegreesToRadians(Swing1LimitDeg);
    const float Swing2 = FMath::DegreesToRadians(Swing2LimitDeg);
    Joint->setTwistLimit(bSoftTwistLimit
        ? PxJointAngularLimitPair(-Twist, Twist, PxSpring(TwistStiffness, TwistDamping))
        : PxJointAngularLimitPair(-Twist, Twist));
    Joint->setSwingLimit(bSoftSwingLimit
        ? PxJointLimitCone(Swing1, Swing2, PxSpring(SwingStiffness, SwingDamping))
        : PxJointLimitCone(Swing1, Swing2));

    // PhysX 관절은 기본으로 두 바디 충돌을 끈다. Disable Collision을 끄면 다시 켠다
    Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, !bDisableCollision);

    // actor0 = 부모. 부모의 역질량·역관성 비율을 0으로 두면 관절 계산에서 부모는 움직이지 않는 것처럼 보인다
    if (bParentDominates)
    {
        Joint->setInvMassScale0(0.0f);
        Joint->setInvInertiaScale0(0.0f);
    }

    Joint->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);

    // 프로젝션: 솔버가 못 지킨 오차가 허용치를 넘으면 자식 바디를 관절 위치로 강제로 되돌린다 (UE 래그돌 기본값과 같은 방식)
    Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, bEnableProjection);
    Joint->setProjectionLinearTolerance(ProjectionLinearTolerance);
    Joint->setProjectionAngularTolerance(FMath::DegreesToRadians(ProjectionAngularToleranceDeg));

    ConstraintHandle = Joint;
    PhysicsScene = Scene;
}

// 관절 해제. 강체(TermBody)보다 먼저 호출해야 한다.
void FConstraintInstance::TermConstraint()
{
    if (!ConstraintHandle)
        return;
    if (PhysicsScene)
        PhysicsScene->WaitPhysScenes();
    FPhysicsInterface::ReleaseJoint(ConstraintHandle);
    PhysicsScene = nullptr;
}
