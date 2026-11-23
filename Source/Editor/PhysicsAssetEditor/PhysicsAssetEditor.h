#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/EditorModeManager.h"
#include "Editor/Gizmo/Gizmo.h"
#include <Editor/AssetEditor/AssetEditor.h>
#include <Core/EngineString.h>
#include <Core/Types.h>

class UPhysicsAsset;
class USceneComponent;

// 에디터가 여는 경로는 .physasset이고, 프리뷰와 편집 대상은 짝이 되는 메시(Foo.fbx.bin)와
// 그 메시가 들고 있는 PhysicsAsset이다 (EditorManager::CreateEditor 참고)
class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }
	const FGizmo* GetPreviewGizmo() const override { return BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }

	// 편집 대상. 메시와 같은 객체라 수정 내용이 씬의 컴포넌트에도 그대로 반영된다
	UPhysicsAsset* GetPhysicsAsset() const;

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 BoneGizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 정리 후 Super

	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BoneGizmo.IsUsing(); }
	void OnBoneSelected() override { BoneGizmo.EndDrag(); SyncPhysicsGizmo(); }
	void CancelViewportInteraction() override { BoneGizmo.EndDrag(); }

private:
	void SyncPhysicsGizmo();
	FEditorModeTools ModeTools;
	FGizmo BoneGizmo{ &ModeTools };
	USceneComponent* BoneGizmoTarget = nullptr;
};
