#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"

namespace
{
	const FVector4 BodyColor(0.35f, 0.65f, 1.0f, 1.0f);           // 하늘색
	const FVector4 SelectedBodyColor(1.0f, 0.8f, 0.2f, 1.0f);     // 선택한 본의 바디는 노랑
	constexpr int32 ArcSegments = 16;

	// 바디 로컬 공간에서 원호 점을 만들고 M으로 월드로 옮겨 잇는다.
	// 점을 행렬로 옮기므로 본의 회전·스케일이 그대로 적용된다 (PhysX에 넣을 때와 같은 Transform)
	void AddLocalArc(FLineBatcher& LineBatcher, const FMatrix& M, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, float StartRad, float EndRad, const FVector4& Color)
	{
		auto PointAt = [&](float T)
		{
			return M.TransformPosition(Center + AxisA * (std::cos(T) * Radius) + AxisB * (std::sin(T) * Radius));
		};
		FVector Prev = PointAt(StartRad);
		for (int32 i = 1; i <= ArcSegments; ++i)
		{
			const FVector P = PointAt(StartRad + (EndRad - StartRad) * i / ArcSegments);
			LineBatcher.AddLine(Prev, P, Color);
			Prev = P;
		}
	}

	// 캡슐: 로컬 Z축이 길이 방향 (MakeCapsule이 Rotation으로 Z를 본의 긴 축에 맞춰 둔다)
	void DrawCapsule(FLineBatcher& LineBatcher, const FKSphylElem& Capsule, const FMatrix& BoneWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BoneWorld;
		const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
		const float R = Capsule.Radius;
		const FVector Top = Z * (Capsule.Length * 0.5f);      // 위쪽 반구 중심
		const FVector Bottom = Z * (Capsule.Length * -0.5f);  // 아래쪽 반구 중심

		// 위·아래 고리
		AddLocalArc(LineBatcher, M, Top, X, Y, R, 0.0f, 2.0f * PI, Color);
		AddLocalArc(LineBatcher, M, Bottom, X, Y, R, 0.0f, 2.0f * PI, Color);
		// 위 반구 (0 → π: X → Z → -X), 아래 반구 (0 → -π: X → -Z → -X)
		AddLocalArc(LineBatcher, M, Top, X, Z, R, 0.0f, PI, Color);
		AddLocalArc(LineBatcher, M, Top, Y, Z, R, 0.0f, PI, Color);
		AddLocalArc(LineBatcher, M, Bottom, X, Z, R, 0.0f, -PI, Color);
		AddLocalArc(LineBatcher, M, Bottom, Y, Z, R, 0.0f, -PI, Color);
		// 옆선 4개
		for (const FVector& Side : { X, X * -1.0f, Y, Y * -1.0f })
		{
			LineBatcher.AddLine(M.TransformPosition(Top + Side * R), M.TransformPosition(Bottom + Side * R), Color);
		}
	}

	void DrawSphere(FLineBatcher& LineBatcher, const FKSphereElem& Sphere, const FMatrix& BoneWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(FQuat::Identity(), Sphere.Center).GetLocalMatrix() * BoneWorld;
		const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
		const FVector Origin = FVector::ZeroVector;
		AddLocalArc(LineBatcher, M, Origin, X, Y, Sphere.Radius, 0.0f, 2.0f * PI, Color);
		AddLocalArc(LineBatcher, M, Origin, X, Z, Sphere.Radius, 0.0f, 2.0f * PI, Color);
		AddLocalArc(LineBatcher, M, Origin, Y, Z, Sphere.Radius, 0.0f, 2.0f * PI, Color);
	}

	// 박스: X/Y/Z는 전체 길이 (PhysicsInterface가 0.5를 곱해 half extent로 넣는다)
	void DrawBox(FLineBatcher& LineBatcher, const FKBoxElem& Box, const FMatrix& BoneWorld, const FVector4& Color)
	{
		const FVector Half(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
		const FMatrix M = FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * BoneWorld;
		LineBatcher.AddOrientedBox(FBox{ Half * -1.0f, Half }, M, Color);
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!Mesh || !MeshComponent || !PhysicsAsset)
	{
		return;
	}

	// PhysX 포즈가 아니라 에셋 데이터 + 본 Transform으로 그린다.
	// 그래서 에디터에서 값을 바꾸면 PhysX를 다시 만들지 않아도 다음 프레임에 바로 보인다
	for (const USkeletalBodySetup* Setup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!Setup)
		{
			continue;
		}

		const int32 BoneIndex = Mesh->FindBoneIndex(Setup->BoneName);
		if (BoneIndex == -1)
		{
			continue;   // 메시에 없는 본 (재임포트로 이름이 바뀐 경우). InitArticulated도 건너뛴다
		}

		const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(BoneIndex);
		const FVector4& Color = BoneIndex == SelectedBoneIndex ? SelectedBodyColor : BodyColor;

		const FKAggregateGeom& Geom = Setup->AggGeom;
		for (const FKSphylElem& Capsule : Geom.SphylElems) DrawCapsule(LineBatcher, Capsule, BoneWorld, Color);
		for (const FKSphereElem& Sphere : Geom.SphereElems) DrawSphere(LineBatcher, Sphere, BoneWorld, Color);
		for (const FKBoxElem& Box : Geom.BoxElems)          DrawBox(LineBatcher, Box, BoneWorld, Color);
		// Convex는 자동 생성에서 쓰지 않아 아직 그리지 않는다
	}
}

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
