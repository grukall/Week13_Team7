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

    const float Twist = FMath::DegreesToRadians(TwistLimitDeg);
    Joint->setTwistLimit(PxJointAngularLimitPair(-Twist, Twist));
    Joint->setSwingLimit(PxJointLimitCone(FMath::DegreesToRadians(Swing1LimitDeg),
                                          FMath::DegreesToRadians(Swing2LimitDeg)));

    Joint->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);

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
