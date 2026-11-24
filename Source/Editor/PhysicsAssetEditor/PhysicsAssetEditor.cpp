#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Render/TriangleBatcher.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"

namespace
{
	const FVector4 SelectedBodyColor(0.2f, 0.55f, 1.0f, 1.0f);    // 선택한 바디 윤곽선 (파랑)
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

// ─────────────────────────────────────────────────────────────
// 반투명 면 (OnRenderTranslucent) · 관절 표시 공용 함수
// ─────────────────────────────────────────────────────────────
namespace
{
	// 알파 = 투명도. 겹쳐도 메시가 비쳐 보이도록 낮게 잡는다
	const FVector4 BodyFillColor(1.0f, 1.0f, 1.0f, 0.18f);          // 흰색
	const FVector4 SelectedBodyFillColor(0.2f, 0.55f, 1.0f, 0.35f);  // 선택: 파랑
	const FVector4 SwingFillColor(1.0f, 0.25f, 0.2f, 0.25f);      // Swing 콘 (빨강)
	const FVector4 TwistFillColor(0.3f, 1.0f, 0.3f, 0.25f);       // Twist 부채꼴 (초록)
	const FVector4 SwingLineColor(1.0f, 0.3f, 0.25f, 1.0f);
	const FVector4 TwistLineColor(0.3f, 1.0f, 0.3f, 1.0f);
	constexpr float SelectedLimitAlphaScale = 1.8f;               // 선택한 관절은 더 진하게
	constexpr float ConstraintSizeScale = 0.35f;                  // 콘·부채꼴 길이 = 자식 캡슐 반지름 × 이 값 (상시 표시라 작게)
	constexpr float SelectedConstraintSizeScale = 1.6f;           // 선택한 관절은 이만큼 더 크게
	constexpr float MinCapsuleSize = 0.005f;                      // 스케일 기즈모로 반지름이 0이 되지 않게
	constexpr int32 FillSegments = 16;                            // 둘레 분할
	constexpr int32 FillRings = 4;                                // 반구 위도 분할

	// 반지름 R인 원 위의 점 (XY 평면, 높이 Z)
	FVector RingPoint(float Angle, float R, float Z)
	{
		return FVector(std::cos(Angle) * R, std::sin(Angle) * R, Z);
	}

	// 로컬 Z축 방향 캡슐을 위도·경도 격자로 쪼개 사각형으로 채운다.
	// 아래 극 → 아래 적도 → (원기둥 옆면) → 위 적도 → 위 극 순서의 고리들을 차례로 잇는다
	void FillCapsule(FTriangleBatcher& TB, const FKSphylElem& Capsule, const FMatrix& BoneWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BoneWorld;
		const float R = Capsule.Radius;
		const float H = Capsule.Length * 0.5f;

		// Ring(j): j = -FillRings(아래 극) ... 0(아래 적도), 1(위 적도) ... FillRings+1(위 극)
		auto Ring = [&](int32 j, float Angle)
		{
			if (j <= 0)
			{
				const float Lat = 0.5f * PI * static_cast<float>(-j) / FillRings;
				return M.TransformPosition(RingPoint(Angle, R * std::cos(Lat), -H - R * std::sin(Lat)));
			}
			const float Lat = 0.5f * PI * static_cast<float>(j - 1) / FillRings;
			return M.TransformPosition(RingPoint(Angle, R * std::cos(Lat), H + R * std::sin(Lat)));
		};

		for (int32 j = -FillRings; j <= FillRings; ++j)
		{
			for (int32 i = 0; i < FillSegments; ++i)
			{
				const float A0 = 2.0f * PI * i / FillSegments;
				const float A1 = 2.0f * PI * (i + 1) / FillSegments;
				TB.AddQuad(Ring(j, A0), Ring(j, A1), Ring(j + 1, A1), Ring(j + 1, A0), Color);
			}
		}
	}

	// 원점 중심 반지름 R 구 (M으로 월드로 옮긴다)
	void FillSphereAt(FTriangleBatcher& TB, const FMatrix& M, float R, const FVector4& Color)
	{
		const int32 Lats = FillRings * 2;
		auto P = [&](int32 j, int32 i)
		{
			const float Lat = -0.5f * PI + PI * j / Lats;
			return M.TransformPosition(RingPoint(2.0f * PI * i / FillSegments, R * std::cos(Lat), R * std::sin(Lat)));
		};
		for (int32 j = 0; j < Lats; ++j)
			for (int32 i = 0; i < FillSegments; ++i)
				TB.AddQuad(P(j, i), P(j, i + 1), P(j + 1, i + 1), P(j + 1, i), Color);
	}

	void FillSphere(FTriangleBatcher& TB, const FKSphereElem& Sphere, const FMatrix& BoneWorld, const FVector4& Color)
	{
		FillSphereAt(TB, FTransform(FQuat::Identity(), Sphere.Center).GetLocalMatrix() * BoneWorld, Sphere.Radius, Color);
	}

	void FillBox(FTriangleBatcher& TB, const FKBoxElem& Box, const FMatrix& BoneWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * BoneWorld;
		const float X = Box.X * 0.5f, Y = Box.Y * 0.5f, Z = Box.Z * 0.5f;
		FVector C[8];
		for (int32 c = 0; c < 8; ++c)
			C[c] = M.TransformPosition(FVector((c & 1) ? X : -X, (c & 2) ? Y : -Y, (c & 4) ? Z : -Z));
		TB.AddQuad(C[0], C[1], C[3], C[2], Color);   // -Z
		TB.AddQuad(C[4], C[5], C[7], C[6], Color);   // +Z
		TB.AddQuad(C[0], C[1], C[5], C[4], Color);   // -Y
		TB.AddQuad(C[2], C[3], C[7], C[6], Color);   // +Y
		TB.AddQuad(C[0], C[2], C[6], C[4], Color);   // -X
		TB.AddQuad(C[1], C[3], C[7], C[5], Color);   // +X
	}

	FVector4 WithAlphaScale(FVector4 Color, float Scale)
	{
		Color.W = std::min(Color.W * Scale, 1.0f);
		return Color;
	}

	// Free는 180°(끝까지), Locked는 0°, Limited는 설정값
	float LimitRad(EAngularMotion Motion, float LimitDeg)
	{
		switch (Motion)
		{
		case EAngularMotion::Free:   return PI;
		case EAngularMotion::Locked: return 0.0f;
		default:                     return FMath::DegreesToRadians(LimitDeg);
		}
	}

	// 관절 하나의 두 프레임을 월드로 옮긴 결과
	struct FConstraintFrames
	{
		FMatrix Parent;      // Frame2 × 부모 본 월드. 제한 범위(콘 · 부채꼴)는 이 프레임에 그린다
		FMatrix Child;       // Frame1 × 자식 본 월드. 현재 각도(화살표)는 이 프레임 방향
		float Length = 0;    // 그릴 크기 (본 로컬 단위. 행렬로 옮기므로 본 스케일이 곱해진다)
		bool bSelected = false;
	};

	bool GetConstraintFrames(const FConstraintInstance& CI, const USkeletalMesh& Mesh, USkeletalMeshComponent& Component,
		const UPhysicsAsset& Asset, bool bSelected, FConstraintFrames& Out)
	{
		const int32 ChildBone = Mesh.FindBoneIndex(CI.ConstraintBone1);
		const int32 ParentBone = Mesh.FindBoneIndex(CI.ConstraintBone2);
		if (ChildBone == -1 || ParentBone == -1)
			return false;

		Out.Child = CI.Frame1.GetLocalMatrix() * Component.GetBoneWorldMatrix(ChildBone);
		Out.Parent = CI.Frame2.GetLocalMatrix() * Component.GetBoneWorldMatrix(ParentBone);

		// 크기는 자식 바디 캡슐 반지름에 비례 (손목 관절은 작게, 골반은 크게)
		float Radius = 0.05f;
		const int32 BodyIndex = Asset.FindBodyIndex(CI.ConstraintBone1);
		if (BodyIndex != -1 && Asset.SkeletalBodySetups[BodyIndex] && !Asset.SkeletalBodySetups[BodyIndex]->AggGeom.SphylElems.IsEmpty())
			Radius = Asset.SkeletalBodySetups[BodyIndex]->AggGeom.SphylElems[0].Radius;
		Out.Length = Radius * ConstraintSizeScale * (bSelected ? SelectedConstraintSizeScale : 1.0f);
		Out.bSelected = bSelected;
		return true;
	}

	// Swing 콘: 부모 프레임 X축을 중심으로 자식 X축이 휠 수 있는 범위.
	// PhysX eSWING1 = Y축 회전 → X가 Z쪽으로 휜다, eSWING2 = Z축 회전 → X가 Y쪽으로 휜다.
	// 그래서 Y 방향 벌어짐 = Swing2, Z 방향 벌어짐 = Swing1이고, 둘이 다르면 밑면이 타원이 된다
	void FillSwingCone(FTriangleBatcher& TB, const FMatrix& Frame, float L, float SpreadY, float SpreadZ, const FVector4& Color)
	{
		if (SpreadY >= PI && SpreadZ >= PI)
		{
			FillSphereAt(TB, Frame, L, Color);   // 둘 다 Free면 모든 방향 → 구
			return;
		}

		TArray<FVector> Rim;
		for (int32 i = 0; i <= FillSegments * 2; ++i)
		{
			const float Phi = 2.0f * PI * i / (FillSegments * 2);
			const float C = std::cos(Phi), S = std::sin(Phi);
			// 방향 Phi에서의 반각: 1/θ² = cos²φ/θy² + sin²φ/θz² (타원)
			const float A = C / std::max(SpreadY, 1e-4f), B = S / std::max(SpreadZ, 1e-4f);
			const float Theta = std::min(1.0f / std::sqrt(A * A + B * B + 1e-12f), PI);
			Rim.Add(Frame.TransformPosition(FVector(std::cos(Theta), std::sin(Theta) * C, std::sin(Theta) * S) * L));
		}
		TB.AddFan(Frame.TransformPosition(FVector::ZeroVector), Rim, Color);
	}

	// Twist 부채꼴: 부모 X축을 축으로 비틀 수 있는 범위. 부모 YZ 평면에 Y축 기준 ±Twist
	void FillTwistFan(FTriangleBatcher& TB, const FMatrix& Frame, float L, float Twist, const FVector4& Color)
	{
		TArray<FVector> Arc;
		for (int32 i = 0; i <= FillSegments * 2; ++i)
		{
			const float A = -Twist + 2.0f * Twist * i / (FillSegments * 2);
			Arc.Add(Frame.TransformPosition(FVector(0.0f, std::cos(A), std::sin(A)) * L));
		}
		TB.AddFan(Frame.TransformPosition(FVector::ZeroVector), Arc, Color);
	}

	// 레이와 캡슐(M 공간의 Z축 선분 ±HalfLength, 반지름 R)이 만나면 레이 위치 t, 아니면 FLT_MAX.
	// 레이를 캡슐 로컬 공간으로 옮겨 "레이 ↔ 중심 선분"의 최단 거리가 R 이하인지 본다.
	// 방향을 정규화하지 않고 옮기므로 t는 월드 레이의 t와 같아, 바디끼리 앞뒤 비교에 그대로 쓸 수 있다
	float RayCapsuleHitT(const FRay& Ray, const FMatrix& M, float R, float HalfLength)
	{
		const FMatrix Inv = M.Inverse();
		const FVector O = Inv.TransformPosition(Ray.Origin);
		const FVector D = Inv.TransformPosition(Ray.Origin + Ray.Direction) - O;

		// 선분 A + s·U (s ∈ [0,1]) 와 레이 O + t·D (t ≥ 0)의 최근접점 (Ericson, Real-Time Collision Detection 5.1.9)
		const FVector A(0.0f, 0.0f, -HalfLength);
		const FVector U(0.0f, 0.0f, 2.0f * HalfLength);
		const FVector W = A - O;
		const float a = U.Dot(U), b = U.Dot(D), c = D.Dot(D), d = U.Dot(W), e = D.Dot(W);
		const float Denom = a * c - b * b;

		float Sc = 0.0f, Tc = 0.0f;
		if (a < 1e-8f)
		{
			Tc = std::max(-e / c, 0.0f);   // 길이 0 = 구: 중심에서 레이로 수선
		}
		else
		{
			Sc = Denom > 1e-8f ? std::clamp((b * e - c * d) / Denom, 0.0f, 1.0f) : 0.0f;
			Tc = (b * Sc + e) / c;
			if (Tc < 0.0f)
			{
				Tc = 0.0f;
				Sc = std::clamp(-d / a, 0.0f, 1.0f);
			}
		}

		const FVector Closest = (A + U * Sc) - (O + D * Tc);
		return Closest.Dot(Closest) <= R * R ? Tc : FLT_MAX;
	}

	FVector FrameAxis(const FMatrix& Frame, const FVector& LocalAxis)
	{
		return (Frame.TransformPosition(LocalAxis) - Frame.TransformPosition(FVector::ZeroVector)).GetSafeNormal();
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!Mesh || !MeshComponent || !PhysicsAsset)
	{
		return;
	}

	// 선택한 바디: 면 위에 윤곽선을 그려 어느 바디인지 또렷하게 한다 (면은 OnRenderTranslucent)
	if (SelectionType == EPhysicsSelection::Body && PhysicsAsset->SkeletalBodySetups.IsValidIndex(SelectedIndex))
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[SelectedIndex];
		const int32 BoneIndex = Setup ? Mesh->FindBoneIndex(Setup->BoneName) : -1;
		if (BoneIndex != -1)
		{
			const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(BoneIndex);
			const FKAggregateGeom& Geom = Setup->AggGeom;
			for (const FKSphylElem& Capsule : Geom.SphylElems) DrawCapsule(LineBatcher, Capsule, BoneWorld, SelectedBodyColor);
			for (const FKSphereElem& Sphere : Geom.SphereElems) DrawSphere(LineBatcher, Sphere, BoneWorld, SelectedBodyColor);
			for (const FKBoxElem& Box : Geom.BoxElems)          DrawBox(LineBatcher, Box, BoneWorld, SelectedBodyColor);
		}
	}

	// 선택한 관절: 현재 각도 화살표 (제한 범위 면은 OnRenderTranslucent)
	if (SelectionType == EPhysicsSelection::Constraint && PhysicsAsset->ConstraintSetup.IsValidIndex(SelectedIndex)
		&& PhysicsAsset->ConstraintSetup[SelectedIndex])
	{
		const FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[SelectedIndex]->DefaultInstance;
		FConstraintFrames Frames;
		if (GetConstraintFrames(CI, *Mesh, *MeshComponent, *PhysicsAsset, true, Frames))
		{
			const FVector Origin = Frames.Parent.TransformPosition(FVector::ZeroVector);
			const float WorldLength = (Frames.Parent.TransformPosition(FVector(Frames.Length, 0.0f, 0.0f)) - Origin).Length();
			const FVector ParentX = FrameAxis(Frames.Parent, FVector(1, 0, 0));

			// Swing 화살표: 자식 X축이 지금 향하는 방향. 빨간 콘 안에 있으면 제한 안이다
			if (CI.Swing1Motion != EAngularMotion::Locked || CI.Swing2Motion != EAngularMotion::Locked)
			{
				LineBatcher.AddLine(Origin, Origin + FrameAxis(Frames.Child, FVector(1, 0, 0)) * WorldLength, SwingLineColor);
			}

			// Twist 화살표: 자식 Y축을 부모 YZ 평면에 투영한 방향. 초록 부채꼴 안에 있으면 제한 안이다
			if (CI.TwistMotion != EAngularMotion::Locked)
			{
				const FVector ChildY = FrameAxis(Frames.Child, FVector(0, 1, 0));
				const FVector Projected = (ChildY - ParentX * ChildY.Dot(ParentX)).GetSafeNormal();
				LineBatcher.AddLine(Origin, Origin + Projected * WorldLength, TwistLineColor);
			}
		}
	}
}

void FPhysicsAssetEditor::OnRenderTranslucent(FTriangleBatcher& TriangleBatcher)
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!Mesh || !MeshComponent || !PhysicsAsset)
	{
		return;
	}

	// 바디: 흰색 반투명 면. 선택한 바디는 파랗고 진하게
	for (int32 i = 0; i < PhysicsAsset->SkeletalBodySetups.Num(); ++i)
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[i];
		const int32 BoneIndex = Setup ? Mesh->FindBoneIndex(Setup->BoneName) : -1;
		if (BoneIndex == -1)
		{
			continue;
		}

		const bool bSelected = SelectionType == EPhysicsSelection::Body && SelectedIndex == i;
		const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(BoneIndex);
		const FVector4& Color = bSelected ? SelectedBodyFillColor : BodyFillColor;

		const FKAggregateGeom& Geom = Setup->AggGeom;
		for (const FKSphylElem& Capsule : Geom.SphylElems) FillCapsule(TriangleBatcher, Capsule, BoneWorld, Color);
		for (const FKSphereElem& Sphere : Geom.SphereElems) FillSphere(TriangleBatcher, Sphere, BoneWorld, Color);
		for (const FKBoxElem& Box : Geom.BoxElems)          FillBox(TriangleBatcher, Box, BoneWorld, Color);
	}

	// 관절 제한: 모든 관절에 작게 상시 표시, 선택한 관절은 크고 진하게. Locked 축은 그리지 않는다 (UE와 같음)
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset->ConstraintSetup[i];
		if (!Template)
		{
			continue;
		}

		const FConstraintInstance& CI = Template->DefaultInstance;
		const bool bSelected = SelectionType == EPhysicsSelection::Constraint && SelectedIndex == i;
		FConstraintFrames Frames;
		if (!GetConstraintFrames(CI, *Mesh, *MeshComponent, *PhysicsAsset, bSelected, Frames))
		{
			continue;
		}
		const float AlphaScale = bSelected ? SelectedLimitAlphaScale : 1.0f;

		if (CI.Swing1Motion != EAngularMotion::Locked || CI.Swing2Motion != EAngularMotion::Locked)
		{
			const float SpreadY = LimitRad(CI.Swing2Motion, CI.Swing2LimitDeg);
			const float SpreadZ = LimitRad(CI.Swing1Motion, CI.Swing1LimitDeg);
			FillSwingCone(TriangleBatcher, Frames.Parent, Frames.Length, SpreadY, SpreadZ, WithAlphaScale(SwingFillColor, AlphaScale));
		}

		if (CI.TwistMotion != EAngularMotion::Locked)
		{
			const float Twist = LimitRad(CI.TwistMotion, CI.TwistLimitDeg);
			FillTwistFan(TriangleBatcher, Frames.Parent, Frames.Length, Twist, WithAlphaScale(TwistFillColor, AlphaScale));
		}
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

	GizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!GizmoTarget) return false;
	GizmoTarget->DetachFromParent();
	return true;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	Gizmo.EndDrag();
	Gizmo.SetTarget(nullptr);
	GizmoTarget = nullptr;
	SelectionType = EPhysicsSelection::None;
	SelectedIndex = -1;
	// 프리뷰 씬 · 뷰포트 정리는 부모가 한다. 빠뜨리면 남는다
	FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	// 기즈모가 입력을 쓰지 않았을 때만 피킹하고, 마지막에 기즈모를 선택 대상 위치로 맞춘다 (SkeletalMeshEditor와 같은 순서)
	if (!UpdatePhysicsGizmo())
	{
		UpdatePhysicsPicking();
	}
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	Gizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::SetSelection(EPhysicsSelection Type, int32 Index)
{
	Gizmo.EndDrag();
	SelectionType = Index >= 0 ? Type : EPhysicsSelection::None;
	SelectedIndex = SelectionType == EPhysicsSelection::None ? -1 : Index;

	// 관절은 스케일이 없으므로 스케일 모드에서 관절을 고르면 이동 모드로 바꿔 준다
	if (SelectionType == EPhysicsSelection::Constraint && ModeTools.GetWidgetMode() == UE::Widget::WM_Scale)
	{
		ModeTools.SetWidgetMode(UE::Widget::WM_Translate);
	}
	SyncPhysicsGizmo();
}

// ─────────────────────────────────────────────────────────────
// 피킹: 관절(화면 거리) → 바디(레이 ↔ 캡슐) 순서
// ─────────────────────────────────────────────────────────────

void FPhysicsAssetEditor::UpdatePhysicsPicking()
{
	if (!CanProcessViewportInput() ||
		!Mesh || !MeshComponent || !GetPhysicsAsset() ||
		!PointerInput.bHovered ||          // 마우스가 뷰포트 위에 없으면 무시
		!PointerInput.bMousePressed ||     // 좌클릭이 시작된 프레임에만
		CameraInput.bCaptured ||           // 카메라 조작 중이면 무시
		IsViewportInteractionBusy() ||     // 기즈모 드래그 중이면 무시
		ViewportWidth == 0 || ViewportHeight == 0)
	{
		return;
	}

	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);

	// 관절은 바디 안쪽에 있어 바디보다 먼저 검사해야 클릭할 수 있다
	const int32 Constraint = PickConstraint(MousePosition);
	if (Constraint != -1)
	{
		SetSelection(EPhysicsSelection::Constraint, Constraint);
		return;
	}

	// 아무것도 안 맞으면 -1 → 선택 해제
	SetSelection(EPhysicsSelection::Body, PickBody(ViewportClient->DeprojectScreenToWorld(MousePosition)));
}

int32 FPhysicsAssetEditor::PickConstraint(const FVector2& MousePosition) const
{
	constexpr float HitPixels = 10.0f;
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	const FMatrix ViewProjection = ViewportClient->GetViewProjectionMatrix();
	const FVector CameraPosition = ViewportClient->GetViewLocation();

	int32 Best = -1;
	float BestDistanceSquared = FLT_MAX;
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset->ConstraintSetup[i];
		FConstraintFrames Frames;
		if (!Template || !GetConstraintFrames(Template->DefaultInstance, *Mesh, *MeshComponent, *PhysicsAsset, false, Frames))
		{
			continue;
		}

		// 관절 원점(부모 프레임 위치)을 화면 좌표로 옮겨 마우스와의 픽셀 거리를 잰다
		const FVector World = Frames.Parent.TransformPosition(FVector::ZeroVector);
		const FVector4 Clip = FVector4(World, 1.0f) * ViewProjection;
		if (Clip.W <= 0.0f)
		{
			continue;   // 카메라 뒤
		}
		const FVector2 Screen((Clip.X / Clip.W * 0.5f + 0.5f) * ViewportWidth, (0.5f - Clip.Y / Clip.W * 0.5f) * ViewportHeight);
		const float DX = Screen.X - MousePosition.X, DY = Screen.Y - MousePosition.Y;
		if (DX * DX + DY * DY > HitPixels * HitPixels)
		{
			continue;
		}

		// 여러 개가 겹치면 카메라에 가까운 관절
		const float DistanceSquared = (World - CameraPosition).SizeSquared();
		if (DistanceSquared < BestDistanceSquared)
		{
			Best = i;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return Best;
}

int32 FPhysicsAssetEditor::PickBody(const FRay& Ray) const
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	int32 Best = -1;
	float BestT = FLT_MAX;

	for (int32 i = 0; i < PhysicsAsset->SkeletalBodySetups.Num(); ++i)
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[i];
		const int32 BoneIndex = Setup ? Mesh->FindBoneIndex(Setup->BoneName) : -1;
		if (BoneIndex == -1)
		{
			continue;
		}
		const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(BoneIndex);

		float T = FLT_MAX;
		for (const FKSphylElem& Capsule : Setup->AggGeom.SphylElems)
		{
			const FMatrix M = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BoneWorld;
			T = std::min(T, RayCapsuleHitT(Ray, M, Capsule.Radius, Capsule.Length * 0.5f));
		}
		for (const FKSphereElem& Sphere : Setup->AggGeom.SphereElems)
		{
			const FMatrix M = FTransform(FQuat::Identity(), Sphere.Center).GetLocalMatrix() * BoneWorld;
			T = std::min(T, RayCapsuleHitT(Ray, M, Sphere.Radius, 0.0f));   // 길이 0 캡슐 = 구
		}

		// 여러 바디가 맞으면 레이에서 가장 앞쪽 (카메라에 가까운) 바디
		if (T < BestT)
		{
			BestT = T;
			Best = i;
		}
	}
	return Best;
}

// ─────────────────────────────────────────────────────────────
// 기즈모: 선택 대상 위치에 놓고, 드래그 결과를 본 공간 값으로 되돌려 에셋에 쓴다
// ─────────────────────────────────────────────────────────────

bool FPhysicsAssetEditor::GetSelectionWorldTransform(FTransform& OutTransform) const
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!Mesh || !MeshComponent || !PhysicsAsset)
	{
		return false;
	}

	if (SelectionType == EPhysicsSelection::Body && PhysicsAsset->SkeletalBodySetups.IsValidIndex(SelectedIndex))
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[SelectedIndex];
		const int32 BoneIndex = Setup ? Mesh->FindBoneIndex(Setup->BoneName) : -1;
		if (BoneIndex == -1 || Setup->AggGeom.SphylElems.IsEmpty())
		{
			return false;   // 지금은 캡슐만 편집한다 (자동 생성이 캡슐만 만든다)
		}
		const FKSphylElem& Capsule = Setup->AggGeom.SphylElems[0];
		OutTransform = FTransform(FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * MeshComponent->GetBoneWorldMatrix(BoneIndex));
	}
	else if (SelectionType == EPhysicsSelection::Constraint && PhysicsAsset->ConstraintSetup.IsValidIndex(SelectedIndex)
		&& PhysicsAsset->ConstraintSetup[SelectedIndex])
	{
		FConstraintFrames Frames;
		if (!GetConstraintFrames(PhysicsAsset->ConstraintSetup[SelectedIndex]->DefaultInstance, *Mesh, *MeshComponent, *PhysicsAsset, true, Frames))
		{
			return false;
		}
		OutTransform = FTransform(Frames.Parent);
	}
	else
	{
		return false;
	}

	// 기즈모 대상의 스케일은 조작 배율로만 쓴다 (1에서 시작해 드래그한 만큼 바뀐다)
	OutTransform.Scale = FVector(1.0f, 1.0f, 1.0f);
	return true;
}

void FPhysicsAssetEditor::SyncPhysicsGizmo()
{
	if (!GizmoTarget)
	{
		Gizmo.SetTarget(nullptr);
		return;
	}
	if (Gizmo.IsUsing())
	{
		return;
	}

	// 관절은 이동 · 회전만 지원한다. 스케일 모드에서는 기즈모를 숨긴다
	const bool bModeSupported = SelectionType != EPhysicsSelection::Constraint
		|| ModeTools.GetWidgetMode() != UE::Widget::WM_Scale;

	FTransform World;
	if (!bModeSupported || !GetSelectionWorldTransform(World))
	{
		Gizmo.SetTarget(nullptr);
		return;
	}
	GizmoTarget->SetTransform(World);
	Gizmo.SetTarget(GizmoTarget);
}

bool FPhysicsAssetEditor::UpdatePhysicsGizmo()
{
	if (!CanProcessViewportInput() || !Gizmo.GetTarget())
	{
		Gizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = Gizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		Gizmo.EndDrag();
		return false;
	}

	SyncPhysicsGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 드래그를 시작하고, 버튼을 놓는 프레임의 마지막 위치도 반영한다
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	Gizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && Gizmo.IsUsing())
	{
		BeginPhysicsGizmoDrag();
	}

	if (Gizmo.IsUsing())
	{
		ApplyPhysicsGizmoTransform();
		if (!PointerInput.bMouseDown)
		{
			Gizmo.EndDrag();
		}
	}

	return bWasUsing || Gizmo.IsUsing() || Gizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::BeginPhysicsGizmoDrag()
{
	// 매 프레임 "드래그 시작 값 + 지금까지의 변화"로 다시 계산해 오차가 쌓이지 않게 시작 값을 저장한다
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	FTransform StartWorld;
	GetSelectionWorldTransform(StartWorld);
	DragStartWorldRotation = StartWorld.Rotation;
	DragStartWorldLocation = StartWorld.Location;

	if (SelectionType == EPhysicsSelection::Body)
	{
		DragStartCapsule = PhysicsAsset->SkeletalBodySetups[SelectedIndex]->AggGeom.SphylElems[0];
	}
	else if (SelectionType == EPhysicsSelection::Constraint)
	{
		const FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[SelectedIndex]->DefaultInstance;
		FConstraintFrames Frames;
		GetConstraintFrames(CI, *Mesh, *MeshComponent, *PhysicsAsset, true, Frames);
		const FTransform ChildWorld(Frames.Child);
		DragStartChildWorldRotation = ChildWorld.Rotation;
		DragStartChildWorldLocation = ChildWorld.Location;
	}
}

void FPhysicsAssetEditor::ApplyPhysicsGizmoTransform()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !GizmoTarget)
	{
		return;
	}

	const FVector TargetLocation = GizmoTarget->GetWorldLocation();
	const FQuat TargetRotation = GizmoTarget->GetRelativeRotationQuat();   // 부모가 없어 relative = world

	// 월드 Transform → 본 공간: 본 월드 행렬의 역행렬을 곱한다 (그릴 때 Local × BoneWorld의 반대)
	const auto ToBoneRotation = [](const FQuat& WorldRotation, const FMatrix& BoneWorld)
	{
		return FTransform(FTransform(WorldRotation, FVector::ZeroVector).GetLocalMatrix() * BoneWorld.Inverse()).Rotation.Normalized();
	};

	if (SelectionType == EPhysicsSelection::Body)
	{
		USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[SelectedIndex];
		const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(Mesh->FindBoneIndex(Setup->BoneName));
		FKSphylElem Capsule = DragStartCapsule;   // 조작한 성분만 바꾸고 나머지는 시작 값 유지

		switch (ModeTools.GetWidgetMode())
		{
		case UE::Widget::WM_Translate:
			Capsule.Center = BoneWorld.Inverse().TransformPosition(TargetLocation);
			break;
		case UE::Widget::WM_Rotate:
			Capsule.Rotation = ToBoneRotation(TargetRotation, BoneWorld);
			break;
		case UE::Widget::WM_Scale:
		{
			// 기즈모 로컬 Z = 캡슐 길이 방향, X · Y = 반지름 방향
			const FVector S = GizmoTarget->GetRelativeScale3D();
			const float RadiusScale = std::abs(S.X - 1.0f) > std::abs(S.Y - 1.0f) ? S.X : S.Y;
			Capsule.Radius = std::max(DragStartCapsule.Radius * RadiusScale, MinCapsuleSize);
			Capsule.Length = std::max(DragStartCapsule.Length * S.Z, 0.0f);
			break;
		}
		default:
			break;
		}
		Setup->AggGeom.SphylElems[0] = Capsule;
	}
	else if (SelectionType == EPhysicsSelection::Constraint)
	{
		// 두 프레임을 같은 월드 변화량만큼 같이 옮긴다. 둘의 상대 위치 · 각도(= 시작 자세)는 그대로라
		// 제한을 벗어난 채로 시작하지 않고, 관절 위치나 콘 · 부채꼴의 방향만 바뀐다
		FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[SelectedIndex]->DefaultInstance;
		const FMatrix ParentBoneWorld = MeshComponent->GetBoneWorldMatrix(Mesh->FindBoneIndex(CI.ConstraintBone2));
		const FMatrix ChildBoneWorld = MeshComponent->GetBoneWorldMatrix(Mesh->FindBoneIndex(CI.ConstraintBone1));

		switch (ModeTools.GetWidgetMode())
		{
		case UE::Widget::WM_Translate:
		{
			// 월드 이동량을 자식 프레임에도 더하고, 각 본 공간 위치로 되돌린다
			const FVector Delta = TargetLocation - DragStartWorldLocation;
			CI.Frame2.Location = ParentBoneWorld.Inverse().TransformPosition(TargetLocation);
			CI.Frame1.Location = ChildBoneWorld.Inverse().TransformPosition(DragStartChildWorldLocation + Delta);
			break;
		}
		case UE::Widget::WM_Rotate:
		{
			const FQuat Delta = (TargetRotation * DragStartWorldRotation.Inverse()).Normalized();
			CI.Frame2.Rotation = ToBoneRotation(TargetRotation, ParentBoneWorld);
			CI.Frame1.Rotation = ToBoneRotation((Delta * DragStartChildWorldRotation).Normalized(), ChildBoneWorld);
			break;
		}
		default:
			break;   // 관절은 스케일이 없다
		}
	}
}
