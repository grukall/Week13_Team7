#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Render/TriangleBatcher.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include <filesystem>
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAssetDebugDraw.h"

namespace
{
	const FVector4 SelectedBodyColor(0.2f, 0.55f, 1.0f, 1.0f);    // 선택한 바디 윤곽선 (파랑)
	// 도형 와이어는 런타임 Show Flag "Physics"와 같은 함수로 그린다
	using PhysicsDebugDraw::DrawCapsule;
	using PhysicsDebugDraw::DrawSphere;
	using PhysicsDebugDraw::DrawBox;
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

	// 관절 표시 크기의 기준 (본 로컬 단위). 첫 캡슐 반지름 → 첫 구 반지름 → 첫 박스의 가장 짧은 변 절반
	float BodyDisplayRadius(const FKAggregateGeom& Geom)
	{
		if (!Geom.SphylElems.IsEmpty())  return Geom.SphylElems[0].Radius;
		if (!Geom.SphereElems.IsEmpty()) return Geom.SphereElems[0].Radius;
		if (!Geom.BoxElems.IsEmpty())
		{
			const FKBoxElem& Box = Geom.BoxElems[0];
			return std::min(Box.X, std::min(Box.Y, Box.Z)) * 0.5f;
		}
		return 0.05f;
	}

	bool GetConstraintFrames(const FConstraintInstance& CI, const USkeletalMesh& Mesh, USkeletalMeshComponent& Component,
		const UPhysicsAsset& Asset, bool bSelected, FConstraintFrames& Out)
	{
		const int32 ChildBone = Mesh.FindBoneIndex(CI.ConstraintBone1);
		const int32 ParentBone = Mesh.FindBoneIndex(CI.ConstraintBone2);
		if (ChildBone == -1 || ParentBone == -1)
			return false;

		Out.Child = CI.Frame1.GetLocalMatrix() * Component.GetBoneWorldMatrix(ChildBone);
		Out.Parent = CI.Frame2.GetLocalMatrix() * Component.GetBoneWorldMatrix(ParentBone);

		// 크기는 자식 바디 도형 크기에 비례 (손목 관절은 작게, 골반은 크게)
		float Radius = 0.05f;
		const int32 BodyIndex = Asset.FindBodyIndex(CI.ConstraintBone1);
		if (BodyIndex != -1 && Asset.SkeletalBodySetups[BodyIndex])
			Radius = BodyDisplayRadius(Asset.SkeletalBodySetups[BodyIndex]->AggGeom);
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
			// 길이 0 = 구: 중심에서 레이로 수선. W = 중심 - O 이므로 수선의 발 t = D·W / D·D = e / c
			// (아래 일반식 Tc = (b·Sc + e) / c 에 Sc = 0을 넣은 것과 같다)
			Tc = std::max(e / c, 0.0f);
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

	// 레이와 박스(M 공간 원점 중심, 반 크기 Half)가 만나면 레이 위치 t, 아니면 FLT_MAX (슬랩 검사).
	// 캡슐과 같이 레이를 박스 로컬로 옮기고 방향을 정규화하지 않아 t가 월드 레이의 t와 같다.
	// 예: X축 슬랩 [-1, 1], O.X = -5, D.X = 2 → t ∈ [2, 3]. 세 축 구간이 겹치는 첫 t가 맞은 위치
	float RayBoxHitT(const FRay& Ray, const FMatrix& M, const FVector& Half)
	{
		const FMatrix Inv = M.Inverse();
		const FVector O = Inv.TransformPosition(Ray.Origin);
		const FVector D = Inv.TransformPosition(Ray.Origin + Ray.Direction) - O;

		float TMin = 0.0f, TMax = FLT_MAX;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(D[Axis]) < 1e-8f)
			{
				if (O[Axis] < -Half[Axis] || O[Axis] > Half[Axis])
					return FLT_MAX;   // 이 축과 나란한데 슬랩 밖
				continue;
			}
			float T1 = (-Half[Axis] - O[Axis]) / D[Axis];
			float T2 = (Half[Axis] - O[Axis]) / D[Axis];
			if (T1 > T2) std::swap(T1, T2);
			TMin = std::max(TMin, T1);
			TMax = std::min(TMax, T2);
			if (TMin > TMax)
				return FLT_MAX;
		}
		return TMin;
	}

	using EPrimitiveType = FPhysicsAssetEditor::EPrimitiveType;
	const FVector4 SelectedPrimitiveColor(1.0f, 0.75f, 0.2f, 1.0f);   // 기즈모가 붙은 도형 윤곽선 (주황)

	int32 NumPrimitives(const FKAggregateGeom& Geom, EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Capsule: return Geom.SphylElems.Num();
		case EPrimitiveType::Sphere:  return Geom.SphereElems.Num();
		default:                      return Geom.BoxElems.Num();
		}
	}

	// 도형 로컬 → 본 공간. 구는 회전이 없어 Identity
	FTransform PrimitiveLocalTransform(const FKAggregateGeom& Geom, EPrimitiveType Type, int32 Index)
	{
		switch (Type)
		{
		case EPrimitiveType::Capsule: return FTransform(Geom.SphylElems[Index].Rotation, Geom.SphylElems[Index].Center);
		case EPrimitiveType::Sphere:  return FTransform(FQuat::Identity(), Geom.SphereElems[Index].Center);
		default:                      return FTransform(Geom.BoxElems[Index].Rotation, Geom.BoxElems[Index].Center);
		}
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
			// 기즈모가 붙은 도형만 주황, 같은 바디의 나머지 도형은 파랑 (도형이 여러 개일 때 무엇을 고치는지 보이게)
			const auto ColorOf = [&](EPrimitiveType Type, int32 Index)
			{
				return SelectedPrimitiveType == Type && SelectedPrimitiveIndex == Index ? SelectedPrimitiveColor : SelectedBodyColor;
			};
			for (int32 i = 0; i < Geom.SphylElems.Num(); ++i)  DrawCapsule(LineBatcher, Geom.SphylElems[i], BoneWorld, ColorOf(EPrimitiveType::Capsule, i));
			for (int32 i = 0; i < Geom.SphereElems.Num(); ++i) DrawSphere(LineBatcher, Geom.SphereElems[i], BoneWorld, ColorOf(EPrimitiveType::Sphere, i));
			for (int32 i = 0; i < Geom.BoxElems.Num(); ++i)    DrawBox(LineBatcher, Geom.BoxElems[i], BoneWorld, ColorOf(EPrimitiveType::Box, i));
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
	const bool bSameBody = SelectionType == EPhysicsSelection::Body && Type == EPhysicsSelection::Body && SelectedIndex == Index;
	SelectionType = Index >= 0 ? Type : EPhysicsSelection::None;
	SelectedIndex = SelectionType == EPhysicsSelection::None ? -1 : Index;
	if (!bSameBody)
	{
		SelectedPrimitiveIndex = -1;   // 다른 바디를 고르면 그 바디의 첫 도형부터 (아래 ValidateSelectedPrimitive)
	}

	// 관절은 스케일이 없으므로 스케일 모드에서 관절을 고르면 이동 모드로 바꿔 준다
	if (SelectionType == EPhysicsSelection::Constraint && ModeTools.GetWidgetMode() == UE::Widget::WM_Scale)
	{
		ModeTools.SetWidgetMode(UE::Widget::WM_Translate);
	}
	ValidateSelectedPrimitive();
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::SelectPrimitive(int32 BodyIndex, EPrimitiveType Type, int32 PrimitiveIndex)
{
	SetSelection(EPhysicsSelection::Body, BodyIndex);
	SelectedPrimitiveType = Type;
	SelectedPrimitiveIndex = PrimitiveIndex;
	ValidateSelectedPrimitive();   // 없는 도형이면 바디의 첫 도형으로
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::ValidateSelectedPrimitive()
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (SelectionType != EPhysicsSelection::Body || !PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(SelectedIndex)
		|| !PhysicsAsset->SkeletalBodySetups[SelectedIndex])
	{
		SelectedPrimitiveIndex = -1;
		return;
	}
	const FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[SelectedIndex]->AggGeom;
	if (SelectedPrimitiveIndex >= 0 && SelectedPrimitiveIndex < NumPrimitives(Geom, SelectedPrimitiveType))
	{
		return;
	}
	for (EPrimitiveType Type : { EPrimitiveType::Capsule, EPrimitiveType::Sphere, EPrimitiveType::Box })
	{
		if (NumPrimitives(Geom, Type) > 0)
		{
			SelectedPrimitiveType = Type;
			SelectedPrimitiveIndex = 0;
			return;
		}
	}
	SelectedPrimitiveIndex = -1;
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

	// 아무것도 안 맞으면 선택 해제
	EPrimitiveType HitType = EPrimitiveType::Capsule;
	int32 HitPrimitive = -1;
	const int32 Body = PickBody(ViewportClient->DeprojectScreenToWorld(MousePosition), HitType, HitPrimitive);
	if (Body == -1)
	{
		SetSelection(EPhysicsSelection::None, -1);
		return;
	}
	SelectPrimitive(Body, HitType, HitPrimitive);
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

int32 FPhysicsAssetEditor::PickBody(const FRay& Ray, EPrimitiveType& OutType, int32& OutPrimitiveIndex) const
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

		// 모든 바디의 모든 도형 중 레이에서 가장 앞쪽 (카메라에 가까운) 도형
		const FKAggregateGeom& Geom = Setup->AggGeom;
		const auto Consider = [&](float T, EPrimitiveType Type, int32 PrimitiveIndex)
		{
			if (T < BestT)
			{
				BestT = T;
				Best = i;
				OutType = Type;
				OutPrimitiveIndex = PrimitiveIndex;
			}
		};
		for (int32 p = 0; p < Geom.SphylElems.Num(); ++p)
		{
			const FKSphylElem& Capsule = Geom.SphylElems[p];
			const FMatrix M = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BoneWorld;
			Consider(RayCapsuleHitT(Ray, M, Capsule.Radius, Capsule.Length * 0.5f), EPrimitiveType::Capsule, p);
		}
		for (int32 p = 0; p < Geom.SphereElems.Num(); ++p)
		{
			const FKSphereElem& Sphere = Geom.SphereElems[p];
			const FMatrix M = FTransform(FQuat::Identity(), Sphere.Center).GetLocalMatrix() * BoneWorld;
			Consider(RayCapsuleHitT(Ray, M, Sphere.Radius, 0.0f), EPrimitiveType::Sphere, p);   // 길이 0 캡슐 = 구
		}
		for (int32 p = 0; p < Geom.BoxElems.Num(); ++p)
		{
			const FKBoxElem& Box = Geom.BoxElems[p];
			const FMatrix M = FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * BoneWorld;
			Consider(RayBoxHitT(Ray, M, FVector(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f)), EPrimitiveType::Box, p);
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
		if (BoneIndex == -1 || SelectedPrimitiveIndex < 0 || SelectedPrimitiveIndex >= NumPrimitives(Setup->AggGeom, SelectedPrimitiveType))
		{
			return false;
		}
		// 기즈모는 고른 도형 하나에 붙는다 (도형 로컬 × 본 월드)
		const FTransform Local = PrimitiveLocalTransform(Setup->AggGeom, SelectedPrimitiveType, SelectedPrimitiveIndex);
		OutTransform = FTransform(Local.GetLocalMatrix() * MeshComponent->GetBoneWorldMatrix(BoneIndex));
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

	// 관절은 이동 · 회전만, 구는 이동 · 스케일만 지원한다 (구는 회전값이 없다). 지원하지 않는 모드에서는 기즈모를 숨긴다
	const UE::Widget::EWidgetMode Mode = ModeTools.GetWidgetMode();
	bool bModeSupported = true;
	if (SelectionType == EPhysicsSelection::Constraint && Mode == UE::Widget::WM_Scale)
		bModeSupported = false;
	if (SelectionType == EPhysicsSelection::Body && SelectedPrimitiveType == EPrimitiveType::Sphere && Mode == UE::Widget::WM_Rotate)
		bModeSupported = false;

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
		const FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[SelectedIndex]->AggGeom;
		switch (SelectedPrimitiveType)
		{
		case EPrimitiveType::Capsule: DragStartCapsule = Geom.SphylElems[SelectedPrimitiveIndex]; break;
		case EPrimitiveType::Sphere:  DragStartSphere = Geom.SphereElems[SelectedPrimitiveIndex]; break;
		case EPrimitiveType::Box:     DragStartBox = Geom.BoxElems[SelectedPrimitiveIndex]; break;
		}
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
		if (SelectedPrimitiveIndex < 0 || SelectedPrimitiveIndex >= NumPrimitives(Setup->AggGeom, SelectedPrimitiveType))
		{
			return;
		}
		const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(Mesh->FindBoneIndex(Setup->BoneName));
		const UE::Widget::EWidgetMode Mode = ModeTools.GetWidgetMode();
		const FVector BoneCenter = BoneWorld.Inverse().TransformPosition(TargetLocation);
		// 기즈모 로컬 축 = 도형 로컬 축. 스케일은 1에서 시작한 배율이다
		const FVector S = GizmoTarget->GetRelativeScale3D();

		// 조작한 성분만 바꾸고 나머지는 드래그 시작 값을 유지한다
		switch (SelectedPrimitiveType)
		{
		case EPrimitiveType::Capsule:
		{
			FKSphylElem Capsule = DragStartCapsule;
			if (Mode == UE::Widget::WM_Translate) Capsule.Center = BoneCenter;
			if (Mode == UE::Widget::WM_Rotate)    Capsule.Rotation = ToBoneRotation(TargetRotation, BoneWorld);
			if (Mode == UE::Widget::WM_Scale)
			{
				// 로컬 Z = 캡슐 길이 방향, X · Y = 반지름 방향
				const float RadiusScale = std::abs(S.X - 1.0f) > std::abs(S.Y - 1.0f) ? S.X : S.Y;
				Capsule.Radius = std::max(DragStartCapsule.Radius * RadiusScale, MinCapsuleSize);
				Capsule.Length = std::max(DragStartCapsule.Length * S.Z, 0.0f);
			}
			Setup->AggGeom.SphylElems[SelectedPrimitiveIndex] = Capsule;
			break;
		}
		case EPrimitiveType::Sphere:
		{
			FKSphereElem Sphere = DragStartSphere;
			if (Mode == UE::Widget::WM_Translate) Sphere.Center = BoneCenter;
			if (Mode == UE::Widget::WM_Scale)
			{
				// 어느 축을 끌어도 반지름 하나. 가장 많이 바뀐 축의 배율을 쓴다 (예: X 1.5 · Y 1 · Z 1 → 반지름 × 1.5)
				float RadiusScale = S.X;
				if (std::abs(S.Y - 1.0f) > std::abs(RadiusScale - 1.0f)) RadiusScale = S.Y;
				if (std::abs(S.Z - 1.0f) > std::abs(RadiusScale - 1.0f)) RadiusScale = S.Z;
				Sphere.Radius = std::max(DragStartSphere.Radius * RadiusScale, MinCapsuleSize);
			}
			Setup->AggGeom.SphereElems[SelectedPrimitiveIndex] = Sphere;
			break;
		}
		case EPrimitiveType::Box:
		{
			FKBoxElem Box = DragStartBox;
			if (Mode == UE::Widget::WM_Translate) Box.Center = BoneCenter;
			if (Mode == UE::Widget::WM_Rotate)    Box.Rotation = ToBoneRotation(TargetRotation, BoneWorld);
			if (Mode == UE::Widget::WM_Scale)
			{
				// 축마다 따로: 로컬 X를 끌면 Box.X만 바뀐다
				Box.X = std::max(DragStartBox.X * S.X, MinCapsuleSize);
				Box.Y = std::max(DragStartBox.Y * S.Y, MinCapsuleSize);
				Box.Z = std::max(DragStartBox.Z * S.Z, MinCapsuleSize);
			}
			Setup->AggGeom.BoxElems[SelectedPrimitiveIndex] = Box;
			break;
		}
		}
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
	MarkDirty();
}

// ─────────────────────────────────────────────────────────────
// 관절 프레임 기본값 (디테일 패널의 ↶ 버튼)
// ─────────────────────────────────────────────────────────────

namespace
{
	using EFrameComponent = FPhysicsAssetEditor::EConstraintFrameComponent;

	bool IsPositionComponent(EFrameComponent Component)
	{
		return Component == EFrameComponent::ChildPosition || Component == EFrameComponent::ParentPosition;
	}
}

bool FPhysicsAssetEditor::SnapConstraintFrameToDefault(int32 ConstraintIndex, EConstraintFrameComponent Component)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->ConstraintSetup.IsValidIndex(ConstraintIndex) || !PhysicsAsset->ConstraintSetup[ConstraintIndex])
	{
		return false;
	}
	FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
	FTransform DefaultFrame1, DefaultFrame2;
	if (!UPhysicsAsset::CalcDefaultConstraintFrames(Mesh, CI.ConstraintBone1, CI.ConstraintBone2, DefaultFrame1, DefaultFrame2, CreateParams.VertWeight))
	{
		return false;
	}

	Gizmo.EndDrag();
	const bool bChild = Component == EFrameComponent::ChildPosition || Component == EFrameComponent::ChildRotation;
	FTransform& Frame = bChild ? CI.Frame1 : CI.Frame2;
	const FTransform& DefaultFrame = bChild ? DefaultFrame1 : DefaultFrame2;
	if (IsPositionComponent(Component))
	{
		Frame.Location = DefaultFrame.Location;
	}
	else
	{
		Frame.Rotation = DefaultFrame.Rotation;
	}
	MarkDirty();
	SyncPhysicsGizmo();
	return true;
}

bool FPhysicsAssetEditor::IsConstraintFrameAtDefault(int32 ConstraintIndex, EConstraintFrameComponent Component) const
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->ConstraintSetup.IsValidIndex(ConstraintIndex) || !PhysicsAsset->ConstraintSetup[ConstraintIndex])
	{
		return true;
	}
	const FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
	FTransform DefaultFrame1, DefaultFrame2;
	if (!UPhysicsAsset::CalcDefaultConstraintFrames(Mesh, CI.ConstraintBone1, CI.ConstraintBone2, DefaultFrame1, DefaultFrame2, CreateParams.VertWeight))
	{
		return true;   // 기본값을 모르면 ↶를 숨긴다
	}

	const bool bChild = Component == EFrameComponent::ChildPosition || Component == EFrameComponent::ChildRotation;
	const FTransform& Frame = bChild ? CI.Frame1 : CI.Frame2;
	const FTransform& DefaultFrame = bChild ? DefaultFrame1 : DefaultFrame2;
	if (IsPositionComponent(Component))
	{
		return (Frame.Location - DefaultFrame.Location).SizeSquared() < 1e-8f;
	}
	// q와 -q는 같은 회전이다
	const FQuat& A = Frame.Rotation;
	const FQuat& B = DefaultFrame.Rotation;
	return std::abs(A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W) > 1.0f - 1e-6f;
}

// ─────────────────────────────────────────────────────────────
// 바디 추가 · 삭제
// ─────────────────────────────────────────────────────────────

bool FPhysicsAssetEditor::AddPrimitiveToBody(int32 BodyIndex, EPrimitiveType Type)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		return false;
	}
	FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[BodyIndex]->AggGeom;

	// 크기 기준 = 바디의 기존 도형 크기 (BodyDisplayRadius). 본 공간 값이라 본 스케일이 큰 메시(Armature 100배 등)에서도
	// 기존 도형과 비슷한 크기가 된다. 위치는 기존 도형과 겹치지 않게 첫 도형 옆(본 로컬 X)으로 지름만큼 띄운다.
	// 도형이 하나도 없으면 본 원점에 반지름 0.05 크기 (UE AddNewPrimitive의 기본 크기 자리)
	const bool bHadPrimitive = !Geom.IsEmpty();
	const float Radius = bHadPrimitive ? BodyDisplayRadius(Geom) : 0.05f;
	FVector Center = FVector::ZeroVector;
	if (bHadPrimitive)
	{
		for (EPrimitiveType Existing : { EPrimitiveType::Capsule, EPrimitiveType::Sphere, EPrimitiveType::Box })
		{
			const int32 Count = NumPrimitives(Geom, Existing);
			if (Count > 0)
			{
				Center = PrimitiveLocalTransform(Geom, Existing, Count - 1).Location + FVector(Radius * 2.0f, 0.0f, 0.0f);
				break;
			}
		}
	}

	int32 NewIndex = -1;
	switch (Type)
	{
	case EPrimitiveType::Capsule:
	{
		FKSphylElem Capsule;
		if (!Geom.SphylElems.IsEmpty())
		{
			Capsule = Geom.SphylElems.Last();   // 마지막 캡슐과 같은 크기 · 방향
		}
		else
		{
			Capsule.Radius = Radius;
			Capsule.Length = Radius * 2.0f;
		}
		Capsule.Center = Center;
		Geom.SphylElems.Add(Capsule);
		NewIndex = Geom.SphylElems.Num() - 1;
		break;
	}
	case EPrimitiveType::Sphere:
	{
		FKSphereElem Sphere;
		Sphere.Radius = Radius;
		Sphere.Center = Center;
		Geom.SphereElems.Add(Sphere);
		NewIndex = Geom.SphereElems.Num() - 1;
		break;
	}
	case EPrimitiveType::Box:
	{
		// 지름 크기의 정육면체 (예: 반지름 0.05 → 0.1 × 0.1 × 0.1)
		FKBoxElem Box;
		Box.X = Box.Y = Box.Z = Radius * 2.0f;
		Box.Center = Center;
		Geom.BoxElems.Add(Box);
		NewIndex = Geom.BoxElems.Num() - 1;
		break;
	}
	}

	SelectPrimitive(BodyIndex, Type, NewIndex);
	MarkDirty();
	return true;
}

bool FPhysicsAssetEditor::DeletePrimitive(int32 BodyIndex, EPrimitiveType Type, int32 PrimitiveIndex)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		return false;
	}
	FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[BodyIndex]->AggGeom;
	const int32 Total = Geom.SphylElems.Num() + Geom.SphereElems.Num() + Geom.BoxElems.Num();
	if (PrimitiveIndex < 0 || PrimitiveIndex >= NumPrimitives(Geom, Type) || Total <= 1)
	{
		return false;   // 마지막 도형은 남긴다. 바디째 지우려면 Delete (DeleteBody)
	}
	Gizmo.EndDrag();

	switch (Type)
	{
	case EPrimitiveType::Capsule: Geom.SphylElems.RemoveAt(PrimitiveIndex); break;
	case EPrimitiveType::Sphere:  Geom.SphereElems.RemoveAt(PrimitiveIndex); break;
	case EPrimitiveType::Box:     Geom.BoxElems.RemoveAt(PrimitiveIndex); break;
	}

	// 인덱스가 밀렸으니 이 바디의 첫 도형을 고른다
	if (SelectionType == EPhysicsSelection::Body && SelectedIndex == BodyIndex)
	{
		SelectedPrimitiveIndex = -1;
		ValidateSelectedPrimitive();
		SyncPhysicsGizmo();
	}
	MarkDirty();
	return true;
}

bool FPhysicsAssetEditor::DeleteBody(int32 BodyIndex)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		return false;
	}
	Gizmo.EndDrag();
	const FString BoneName = PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName;

	// 이 바디를 자식(Bone1)이나 부모(Bone2)로 쓰는 관절을 모두 지운다. 뒤에서부터 지워야 인덱스가 안 밀린다.
	// 예) spine_03을 지우면 spine_02–spine_03, spine_03–Head, spine_03–upperarm_l/r 관절이 함께 사라지고
	//     Head · upperarm은 관절 없이 남는다 (UE와 같음. 새 관절은 바디 우클릭 Add Constraint로 잇는다)
	TArray<UPhysicsConstraintTemplate*>& Constraints = PhysicsAsset->ConstraintSetup;
	for (int32 i = Constraints.Num() - 1; i >= 0; --i)
	{
		const UPhysicsConstraintTemplate* Template = Constraints[i];
		if (!Template || Template->DefaultInstance.ConstraintBone1 == BoneName || Template->DefaultInstance.ConstraintBone2 == BoneName)
		{
			Constraints.RemoveAt(i);
		}
	}

	// 객체는 delete하지 않고 배열에서만 뺀다. 같은 에셋으로 이미 만들어진 씬 컴포넌트의 FBodyInstance가
	// BodySetup 포인터를 들고 있어서, 지우면 그 컴포넌트를 다시 만들기 전까지 끊어진 포인터가 된다
	PhysicsAsset->SkeletalBodySetups.RemoveAt(BodyIndex);
	PhysicsAsset->UpdateBodySetupIndexMap();   // 빠뜨리면 FindBodyIndex가 밀린 인덱스를 돌려준다

	// 인덱스가 밀렸으니 선택은 해제한다
	SetSelection(EPhysicsSelection::None, -1);
	MarkDirty();
	return true;
}

bool FPhysicsAssetEditor::DeleteConstraint(int32 ConstraintIndex)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->ConstraintSetup.IsValidIndex(ConstraintIndex))
	{
		return false;
	}
	Gizmo.EndDrag();

	// 바디와 같은 이유로 객체는 delete하지 않고 배열에서만 뺀다
	PhysicsAsset->ConstraintSetup.RemoveAt(ConstraintIndex);

	// 관절 인덱스가 밀렸으니 관절 선택은 해제한다 (바디 선택은 인덱스가 그대로라 유지)
	if (SelectionType == EPhysicsSelection::Constraint)
	{
		SetSelection(EPhysicsSelection::None, -1);
	}
	MarkDirty();
	return true;
}

void FPhysicsAssetEditor::DeleteSelection()
{
	if (Gizmo.IsUsing())
	{
		return;
	}
	if (SelectionType == EPhysicsSelection::Body)
	{
		DeleteBody(SelectedIndex);
	}
	else if (SelectionType == EPhysicsSelection::Constraint)
	{
		DeleteConstraint(SelectedIndex);
	}
}

// ─────────────────────────────────────────────────────────────
// 저장
// ─────────────────────────────────────────────────────────────

bool FPhysicsAssetEditor::Save()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		SaveMessage = "Save failed: no physics asset";
		return false;
	}
	Gizmo.EndDrag();

	const FString Resolved = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	const std::filesystem::path FilePath = FPaths::FromUtf8(Resolved.empty() ? GetAssetPath() : Resolved);
	std::filesystem::path TempPath = FilePath;
	TempPath += ".saving";

	// 임시 파일에 쓰고 바꿔치기한다. 쓰다가 실패해도 원본 .physasset은 그대로 남는다 (AnimSequenceEditor와 같은 방식)
	FString Error;
	if (!PhysicsAssetFile::Write(TempPath, *PhysicsAsset, Error))
	{
		std::error_code Ignored;
		std::filesystem::remove(TempPath, Ignored);
		SaveMessage = "Save failed: " + Error;
		HTR_LOG(Error, "PhysicsAsset save failed: {} ({})", GetAssetPath(), Error);
		return false;
	}
	std::error_code Ec;
	std::filesystem::rename(TempPath, FilePath, Ec);   // 같은 볼륨이면 기존 파일을 교체한다
	if (Ec)
	{
		std::error_code Ignored;
		std::filesystem::remove(TempPath, Ignored);
		SaveMessage = "Save failed: cannot replace file (" + Ec.message() + ")";
		HTR_LOG(Error, "PhysicsAsset save failed: {} ({})", GetAssetPath(), Ec.message());
		return false;
	}

	// 이 메시를 쓰고 물리 상태가 있는 컴포넌트(레벨 · PIE · 프리뷰)의 바디와 관절을 새 설정으로 다시 만든다.
	// 지운 바디를 가리키던 FBodyInstance도 이때 사라진다
	int32 Refreshed = 0;
	for (TObjectIterator<USkeletalMeshComponent> Itr; Itr; ++Itr)
	{
		USkeletalMeshComponent* Component = *Itr;
		if (Component && Component->GetSkeletalMesh() == Mesh && Component->IsPhysicsStateCreated())
		{
			Component->RecreatePhysicsState();
			++Refreshed;
		}
	}

	bDirty = false;
	SaveMessage = "Saved.";
	HTR_LOG(Info, "PhysicsAsset saved: {} ({} components refreshed)", GetAssetPath(), Refreshed);
	return true;
}

// ─────────────────────────────────────────────────────────────
// 전체 재생성
// ─────────────────────────────────────────────────────────────

bool FPhysicsAssetEditor::RegenerateAllBodies()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !Mesh)
	{
		return false;
	}
	UPhysicsAsset* Generated = UPhysicsAsset::CreateFromSkeletalMesh(Mesh, CreateParams);
	if (!Generated)
	{
		// 기준값이 커서 바디가 하나도 안 나오면 생성기가 nullptr을 돌려준다. 지금 에셋은 그대로 둔다
		SaveMessage = "Regenerate failed: no bone passed the size limits";
		HTR_LOG(Warning, "PhysicsAsset regenerate produced no bodies: {}", GetAssetPath());
		return false;
	}
	Gizmo.EndDrag();

	// 메시 · 씬 컴포넌트 · 이 에디터가 모두 같은 UPhysicsAsset 객체를 가리키므로, 객체는 그대로 두고 내용만 바꾼다.
	// 이전 바디 · 관절 객체는 delete하지 않는다 (씬 컴포넌트의 FBodyInstance가 저장 전까지 포인터를 들고 있다)
	PhysicsAsset->SkeletalBodySetups = Generated->SkeletalBodySetups;
	PhysicsAsset->ConstraintSetup = Generated->ConstraintSetup;
	for (USkeletalBodySetup* Setup : PhysicsAsset->SkeletalBodySetups)
	{
		if (Setup) Setup->SetOuter(PhysicsAsset);
	}
	for (UPhysicsConstraintTemplate* Template : PhysicsAsset->ConstraintSetup)
	{
		if (Template) Template->SetOuter(PhysicsAsset);
	}
	PhysicsAsset->UpdateBodySetupIndexMap();

	// 내용을 넘겨받았으니 임시 에셋만 지운다 (배열을 비워 둬야 새 바디까지 지워지지 않는다)
	Generated->SkeletalBodySetups.Empty();
	Generated->ConstraintSetup.Empty();
	delete Generated;

	SetSelection(EPhysicsSelection::None, -1);
	MarkDirty();
	SaveMessage.clear();
	HTR_LOG(Info, "PhysicsAsset regenerated: {} bodies, {} constraints", PhysicsAsset->SkeletalBodySetups.Num(), PhysicsAsset->ConstraintSetup.Num());
	return true;
}

// ─────────────────────────────────────────────────────────────
// 바디 메뉴: 바디 재생성 · 관절 추가 · 피직스 타입
// ─────────────────────────────────────────────────────────────

bool FPhysicsAssetEditor::RegenerateBody(int32 BodyIndex)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		return false;
	}
	USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
	FKAggregateGeom Geom;
	if (!UPhysicsAsset::CalcBoneGeom(Mesh, Setup->BoneName, CreateParams.VertWeight, CreateParams.GeomType, Geom))
	{
		SaveMessage = "Regenerate body failed: bone has no skinned vertices";
		return false;
	}
	Gizmo.EndDrag();

	// 도형만 새로 만든다 (UE "바디 재생성"과 같다). 피직스 타입 · 질량 같은 바디 설정과 관절은 그대로다
	Setup->AggGeom = Geom;   // Tool 패널 Primitive Type 도형 하나로 바꾼다

	SetSelection(EPhysicsSelection::None, -1);   // 도형 인덱스가 바뀌었으니 고른 도형을 첫 도형으로 다시 잡는다
	SetSelection(EPhysicsSelection::Body, BodyIndex);
	MarkDirty();
	return true;
}

bool FPhysicsAssetEditor::HasConstraintBetween(int32 BodyIndexA, int32 BodyIndexB) const
{
	const UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndexA) || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndexB)
		|| !PhysicsAsset->SkeletalBodySetups[BodyIndexA] || !PhysicsAsset->SkeletalBodySetups[BodyIndexB])
	{
		return false;
	}
	const FString& A = PhysicsAsset->SkeletalBodySetups[BodyIndexA]->BoneName;
	const FString& B = PhysicsAsset->SkeletalBodySetups[BodyIndexB]->BoneName;
	for (const UPhysicsConstraintTemplate* Template : PhysicsAsset->ConstraintSetup)
	{
		if (!Template) continue;
		const FConstraintInstance& CI = Template->DefaultInstance;
		if ((CI.ConstraintBone1 == A && CI.ConstraintBone2 == B) || (CI.ConstraintBone1 == B && CI.ConstraintBone2 == A))
		{
			return true;
		}
	}
	return false;
}

int32 FPhysicsAssetEditor::AddConstraint(int32 ChildBodyIndex, int32 ParentBodyIndex)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || ChildBodyIndex == ParentBodyIndex
		|| !PhysicsAsset->SkeletalBodySetups.IsValidIndex(ChildBodyIndex) || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(ParentBodyIndex)
		|| !PhysicsAsset->SkeletalBodySetups[ChildBodyIndex] || !PhysicsAsset->SkeletalBodySetups[ParentBodyIndex]
		|| HasConstraintBetween(ChildBodyIndex, ParentBodyIndex))
	{
		return -1;
	}

	auto* Template = NewObject<UPhysicsConstraintTemplate>(PhysicsAsset);
	FConstraintInstance& CI = Template->DefaultInstance;
	CI.ConstraintBone1 = PhysicsAsset->SkeletalBodySetups[ChildBodyIndex]->BoneName;
	CI.ConstraintBone2 = PhysicsAsset->SkeletalBodySetups[ParentBodyIndex]->BoneName;

	// 자동 생성과 같은 프레임: 자식 본 원점에 관절을 두고, 바인드 포즈에서 두 프레임이 같은 월드 위치 · 방향이 되게 한다.
	// 부모가 본 계층의 조상이 아니어도 (예: hand_l ↔ thigh_l) 같은 식으로 맞춰진다
	if (!UPhysicsAsset::CalcDefaultConstraintFrames(Mesh, CI.ConstraintBone1, CI.ConstraintBone2, CI.Frame1, CI.Frame2, CreateParams.VertWeight))
	{
		delete Template;
		return -1;
	}
	CI.Swing1Motion = CI.Swing2Motion = CI.TwistMotion = CreateParams.AngularConstraintMode;

	PhysicsAsset->ConstraintSetup.Add(Template);
	const int32 NewIndex = PhysicsAsset->ConstraintSetup.Num() - 1;
	SetSelection(EPhysicsSelection::Constraint, NewIndex);
	MarkDirty();
	return NewIndex;
}

void FPhysicsAssetEditor::SetBodyPhysicsType(int32 BodyIndex, EPhysicsType Type, bool bIncludeBelow)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset || !Mesh || !PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		return;
	}
	const int32 RootBone = Mesh->FindBoneIndex(PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName);
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	// 본 계층에서 RootBone이 Bone의 조상(또는 자신)인지
	const auto IsAtOrBelow = [&](int32 Bone)
	{
		for (; Bone != -1; Bone = Bones[Bone].ParentIndex)
		{
			if (Bone == RootBone) return true;
		}
		return false;
	};

	bool bChanged = false;
	for (int32 i = 0; i < PhysicsAsset->SkeletalBodySetups.Num(); ++i)
	{
		USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[i];
		if (!Setup) continue;
		const bool bTarget = i == BodyIndex || (bIncludeBelow && RootBone != -1 && IsAtOrBelow(Mesh->FindBoneIndex(Setup->BoneName)));
		if (bTarget && Setup->PhysicsType != Type)
		{
			Setup->PhysicsType = Type;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkDirty();
	}
}
