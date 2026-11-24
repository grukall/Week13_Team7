#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/EditorModeManager.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Physics/BodySetup.h"
#include <Editor/AssetEditor/AssetEditor.h>
#include <Core/EngineString.h>
#include <Core/Types.h>

class UPhysicsAsset;
class USceneComponent;
class FLineBatcher;
class FTriangleBatcher;
struct FRay;

// 에디터가 여는 경로는 .physasset이고, 프리뷰와 편집 대상은 짝이 되는 메시(Foo.fbx.bin)와
// 그 메시가 들고 있는 PhysicsAsset이다 (EditorManager::CreateEditor 참고)
class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	// 뷰포트에서 선택한 대상. 인덱스는 SkeletalBodySetups / ConstraintSetup의 인덱스
	enum class EPhysicsSelection : uint8 { None, Body, Constraint };

	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	void OnRenderLine(FLineBatcher& LineBatcher) override;
	void OnRenderTranslucent(FTriangleBatcher& TriangleBatcher) override;
	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }
	const FGizmo* GetPreviewGizmo() const override { return Gizmo.GetTarget() ? &Gizmo : nullptr; }

	// 편집 대상. 메시와 같은 객체라 수정 내용이 씬의 컴포넌트에도 그대로 반영된다
	UPhysicsAsset* GetPhysicsAsset() const;
	bool IsGizmoDragging() const { return Gizmo.IsUsing(); }

	// 기즈모 모드 (뷰포트 툴바 · W/E/R · Space). 관절은 스케일 모드에서 기즈모가 숨는다
	void SetGizmoMode(UE::Widget::EWidgetMode Mode);
	UE::Widget::EWidgetMode GetGizmoMode() const { return ModeTools.GetWidgetMode(); }

	// 선택 (Index가 -1이면 해제). 스케일 모드에서 관절을 고르면 이동 모드로 바뀐다
	void SetSelection(EPhysicsSelection Type, int32 Index);
	EPhysicsSelection GetSelectionType() const { return SelectionType; }
	int32 GetSelectedIndex() const { return SelectedIndex; }

	// 저장하지 않은 변경. 디테일 패널 · 기즈모가 에셋을 고치면 부른다 (탭 이름에 *)
	void MarkDirty() { bDirty = true; }
	bool IsDirty() const override { return bDirty; }

	// .physasset에 쓰고, 이 메시를 쓰는 씬 컴포넌트의 PhysX 바디 · 관절을 새 값으로 다시 만든다 (툴바 Save · Ctrl+S)
	bool Save();
	const FString& GetSaveMessage() const { return SaveMessage; }

	// 바디 편집 (스켈레톤 트리 우클릭 메뉴 · Delete 키)
	bool AddCapsuleToBody(int32 BodyIndex);     // 바디에 캡슐 하나를 더한다 (지금 지원하는 프리미티브는 캡슐뿐)
	bool DeleteBody(int32 BodyIndex);           // 바디와, 그 바디를 부모나 자식으로 잇던 관절을 모두 지운다
	void DeleteSelection();                     // Delete 키. 바디만 지운다 (관절 삭제는 그래프 패널에서)

	// 관절 프레임 하나의 위치 또는 회전을 기본값(자동 생성과 같은 값)으로 되돌린다
	enum class EConstraintFrameComponent : uint8 { ChildPosition, ChildRotation, ParentPosition, ParentRotation };
	bool SnapConstraintFrameToDefault(int32 ConstraintIndex, EConstraintFrameComponent Component);
	bool IsConstraintFrameAtDefault(int32 ConstraintIndex, EConstraintFrameComponent Component) const;

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 GizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 · 선택 정리 후 Super

	// 본 피킹 대신 바디 · 관절 피킹과 기즈모 편집을 한다
	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return Gizmo.IsUsing(); }
	void CancelViewportInteraction() override { Gizmo.EndDrag(); }

private:
	// 피킹
	void UpdatePhysicsPicking();
	int32 PickConstraint(const FVector2& MousePosition) const;   // 관절 원점과 마우스의 화면 거리
	int32 PickBody(const FRay& Ray) const;                        // 레이 ↔ 캡슐

	// 기즈모
	bool GetSelectionWorldTransform(FTransform& OutTransform) const;
	void SyncPhysicsGizmo();
	bool UpdatePhysicsGizmo();               // 기즈모가 입력을 썼으면 true (그 프레임은 피킹하지 않는다)
	void BeginPhysicsGizmoDrag();
	void ApplyPhysicsGizmoTransform();

	EPhysicsSelection SelectionType = EPhysicsSelection::None;
	int32 SelectedIndex = -1;
	bool bDirty = false;
	FString SaveMessage;                     // 마지막 저장 결과 ("Saved." / 실패 이유)

	FEditorModeTools ModeTools;
	FGizmo Gizmo{ &ModeTools };
	USceneComponent* GizmoTarget = nullptr;

	// 드래그 시작 값. 매 프레임 "시작 값 + 변화"로 다시 계산한다
	FKSphylElem DragStartCapsule;
	FQuat DragStartWorldRotation = FQuat::Identity();
	FQuat DragStartChildWorldRotation = FQuat::Identity();
	FVector DragStartWorldLocation = FVector::ZeroVector;
	FVector DragStartChildWorldLocation = FVector::ZeroVector;
};
