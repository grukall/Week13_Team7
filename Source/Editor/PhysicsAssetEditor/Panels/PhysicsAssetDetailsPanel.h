#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Containers/Array.h"
#include "Math/Transform.h"

class UObject;
class UPhysicsAsset;
class USkeletalBodySetup;
class UPhysicsConstraintTemplate;
struct FKSphylElem;
struct FKSphereElem;
struct FKBoxElem;

// 선택한 바디 · 관절의 값을 고치는 패널 (UE Physics Asset Editor의 Details 탭).
//   선택 없음  → 에셋 요약
//   Body       → Body Setup · Physics · Collision (리플렉션) + Primitives › Capsule · Sphere · Box (직접 그림)
//   Constraint → Constraint (본 이름) + Constraint Transforms (직접 그림) + Angular Limits
// 편집 대상은 메시가 들고 있는 UPhysicsAsset 그 자체라 뷰포트 · 기즈모에 다음 프레임 바로 보인다.
// 리플렉션이 못 그리는 것 (TArray, FQuat)만 직접 그린다
class FPhysicsAssetDetailsPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawAssetSummary(const UPhysicsAsset& Asset);
	void DrawBodyDetails(USkeletalBodySetup& Setup);
	void DrawConstraintDetails(UPhysicsConstraintTemplate& Template, int32 ConstraintIndex);

	// 도형 하나. 헤더를 누르면 그 도형을 골라 기즈모가 옮겨 가고, 우클릭 메뉴로 지운다
	bool BeginPrimitive(const char* TypeName, uint8 Type, int32 PrimitiveIndex, bool bCanDelete);
	bool DrawCapsule(FKSphylElem& Capsule, int32 CapsuleIndex, bool bCanDelete);
	bool DrawSphere(FKSphereElem& Sphere, int32 SphereIndex, bool bCanDelete);
	bool DrawBox(FKBoxElem& Box, int32 BoxIndex, bool bCanDelete);
	// 관절 프레임 한 쪽 (Child = Frame1, Parent = Frame2): 위치 · 회전 두 줄과 각 줄의 ↶ (기본값으로 스냅)
	bool DrawConstraintFrame(FTransform& Frame, int32 ConstraintIndex, bool bChild);

	// 오일러 입력 캐시. 매 프레임 쿼터니언 → 오일러로 바꾸면 Pitch 90° 근처에서 값이 튄다.
	// 캐시는 쿼터니언이 바깥(기즈모 · 스냅)에서 바뀌었을 때만 다시 계산한다
	FRotationConversionCache ChildFrameRotation;
	FRotationConversionCache ParentFrameRotation;
	TArray<FRotationConversionCache> CapsuleRotations;
	TArray<FRotationConversionCache> BoxRotations;

	// 우클릭 메뉴에서 고른 도형 삭제 (패널을 다 그린 뒤 실행). Index -1 = 없음
	uint8 PendingDeleteType = 0;
	int32 PendingDeleteIndex = -1;
};
