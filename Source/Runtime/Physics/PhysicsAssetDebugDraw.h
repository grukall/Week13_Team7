#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"

class FLineBatcher;
class UWorld;
struct FKSphylElem;
struct FKSphereElem;
struct FKBoxElem;
struct FKAggregateGeom;

// Physics Asset 바디 도형을 와이어로 그리는 공용 함수.
// Physics Asset 에디터(에셋 값 × 본 행렬)와 런타임 Show Flag "Physics"(PhysX 바디 위치)가 같이 쓴다.
// BodyWorld = 바디(본) 월드 행렬. 도형 값은 본 공간이라 그릴 때 "도형 로컬 × BodyWorld"로 옮긴다
namespace PhysicsDebugDraw
{
	void DrawCapsule(FLineBatcher& LineBatcher, const FKSphylElem& Capsule, const FMatrix& BodyWorld, const FVector4& Color);
	void DrawSphere(FLineBatcher& LineBatcher, const FKSphereElem& Sphere, const FMatrix& BodyWorld, const FVector4& Color);
	void DrawBox(FLineBatcher& LineBatcher, const FKBoxElem& Box, const FMatrix& BodyWorld, const FVector4& Color);
	// 바디 하나의 모든 도형 (캡슐 · 구 · 박스)
	void DrawAggGeom(FLineBatcher& LineBatcher, const FKAggregateGeom& Geom, const FMatrix& BodyWorld, const FVector4& Color);
}

// Show Flag "Physics": World의 모든 스켈레탈 메시 래그돌 바디를 실제 PhysX 위치로 그린다.
// 시뮬레이션 중인 바디는 주황, 애니메이션을 따르는(Kinematic) 바디는 하늘색.
// PhysX 바디가 없는 에디터 월드에서는 에셋 값 × 본 위치로 그린다 (하늘색)
void DrawSkeletalPhysicsDebug(UWorld* World, FLineBatcher* LineBatcher);
