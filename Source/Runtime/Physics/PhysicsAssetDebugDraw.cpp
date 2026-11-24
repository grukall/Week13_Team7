#include "EnginePCH.h"
#include "Physics/PhysicsAssetDebugDraw.h"
#include "Physics/BodySetup.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"

namespace
{
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
}

namespace PhysicsDebugDraw
{
	// 캡슐: 로컬 Z축이 길이 방향 (MakeCapsule이 Rotation으로 Z를 본의 긴 축에 맞춰 둔다)
	void DrawCapsule(FLineBatcher& LineBatcher, const FKSphylElem& Capsule, const FMatrix& BodyWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BodyWorld;
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

	void DrawSphere(FLineBatcher& LineBatcher, const FKSphereElem& Sphere, const FMatrix& BodyWorld, const FVector4& Color)
	{
		const FMatrix M = FTransform(FQuat::Identity(), Sphere.Center).GetLocalMatrix() * BodyWorld;
		const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
		const FVector Origin = FVector::ZeroVector;
		AddLocalArc(LineBatcher, M, Origin, X, Y, Sphere.Radius, 0.0f, 2.0f * PI, Color);
		AddLocalArc(LineBatcher, M, Origin, X, Z, Sphere.Radius, 0.0f, 2.0f * PI, Color);
		AddLocalArc(LineBatcher, M, Origin, Y, Z, Sphere.Radius, 0.0f, 2.0f * PI, Color);
	}

	// 박스: X/Y/Z는 전체 길이 (PhysicsInterface가 0.5를 곱해 half extent로 넣는다)
	void DrawBox(FLineBatcher& LineBatcher, const FKBoxElem& Box, const FMatrix& BodyWorld, const FVector4& Color)
	{
		const FVector Half(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
		const FMatrix M = FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * BodyWorld;
		LineBatcher.AddOrientedBox(FBox{ Half * -1.0f, Half }, M, Color);
	}

	void DrawAggGeom(FLineBatcher& LineBatcher, const FKAggregateGeom& Geom, const FMatrix& BodyWorld, const FVector4& Color)
	{
		for (const FKSphylElem& Capsule : Geom.SphylElems) DrawCapsule(LineBatcher, Capsule, BodyWorld, Color);
		for (const FKSphereElem& Sphere : Geom.SphereElems) DrawSphere(LineBatcher, Sphere, BodyWorld, Color);
		for (const FKBoxElem& Box : Geom.BoxElems)          DrawBox(LineBatcher, Box, BodyWorld, Color);
	}
}

void DrawSkeletalPhysicsDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawPhysicsDebug(*LineBatcher);
	}
}
