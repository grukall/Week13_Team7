#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"

UPhysicsAsset* FPhysicsAssetEditor::GetPhysicsAsset() const
{
	return Mesh ? Mesh->GetPhysicsAsset() : nullptr;
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	// TUniquePtr<T>()는 빈 포인터라 FAssetEditor::Initialize가 실패한다. 실제 객체를 만들어 넘긴다
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

bool FPhysicsAssetEditor::InitializeResources()
{
	// 프리뷰 씬 · 메시 컴포넌트 · 본 목록 생성 (FSkeletalMeshEditor와 같은 순서)
	if (!InitializePreview()) return false;
	if (!GetPhysicsAsset()) return false;

	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	return true;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	// 프리뷰 씬 · 뷰포트 정리는 부모가 한다. 빠뜨리면 남는다
	FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	UpdateBonePicking();
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::SyncPhysicsGizmo()
{
	if (!MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}
	if (BoneGizmo.IsUsing()) return;

	// 지금은 선택한 본 위치에 표시만 한다. 드래그로 바디/관절 프레임을 고치는 건 편집 기능 때 연결
	FTransform BoneWorld(MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex));
	BoneWorld.Scale = FVector(1.0f, 1.0f, 1.0f);
	BoneGizmoTarget->SetTransform(BoneWorld);
	BoneGizmo.SetTarget(BoneGizmoTarget);
}
