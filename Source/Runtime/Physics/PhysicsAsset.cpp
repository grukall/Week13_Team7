#include "EnginePCH.h"
#include "PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include <Containers/Array.h>
#include <Render/SkeletalMeshData.h>
#include <Math/Matrix.h>
#include "BodySetup.h"
#include <Math/EngineMath.h>
#include "ConstraintInstance.h"
#include <Math/Transform.h>
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 Version = Ar.IsSaving() ? 1 : 0;
	Record << SA_VALUE("Version", Version);
	if (Version != 1)
	{
		Ar.SetError();
		return;
	}

	// 같은 객체에 다시 읽는 경우(되돌리기 등) 이전 목록을 비운다
	if (Ar.IsLoading())
	{
		SkeletalBodySetups.Reset();
		ConstraintSetup.Reset();
	}

	int32 NumBodies = Ar.IsSaving() ? static_cast<int32>(SkeletalBodySetups.Num()) : 0;
	auto Bodies = Record.EnterArray("Bodies", NumBodies);
	for (int32 i = 0; i < NumBodies; ++i)
	{
		if (Ar.IsLoading())
			SkeletalBodySetups.Add(NewObject<USkeletalBodySetup>(this));
		SkeletalBodySetups[i]->Serialize(Bodies.EnterElement().EnterRecord());
	}

	int32 NumConstraints = Ar.IsSaving() ? static_cast<int32>(ConstraintSetup.Num()) : 0;
	auto Constraints = Record.EnterArray("Constraints", NumConstraints);
	for (int32 i = 0; i < NumConstraints; ++i)
	{
		if (Ar.IsLoading())
			ConstraintSetup.Add(NewObject<UPhysicsConstraintTemplate>(this));
		ConstraintSetup[i]->Serialize(Constraints.EnterElement().EnterRecord());
	}

	// 맵은 저장하지 않고 다시 만든다. 빠뜨리면 FindBodyIndex가 전부 -1이 된다
	if (Ar.IsLoading())
		UpdateBodySetupIndexMap();
}

// 본 이름 → SkeletalBodySetups 인덱스. 바디가 없는 본이면 -1
int32 UPhysicsAsset::FindBodyIndex(const FString& BoneName) const
{
	const int32* Found = BodySetupIndexMap.Find(BoneName);
	return Found ? *Found : -1;
}

// SkeletalBodySetups를 추가/삭제/로드한 뒤 반드시 호출
void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.Empty(SkeletalBodySetups.Num());
	for (int32 i = 0; i < SkeletalBodySetups.Num(); ++i)
	{
		if (const USkeletalBodySetup* Setup = SkeletalBodySetups[i])
			BodySetupIndexMap.FindOrAdd(Setup->BoneName) = i;
	}
}

namespace
{
    //정점이 하나도 없는 본은 박스가 뒤집혀 있다.
    static bool HasVertices(const FBox& Box)
    {
        return Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
    }

    FKSphylElem MakeCapsule(const FBox& BoundsCS, const FMatrix& InverseBindPose, int32& OutAxis)
    {
        //8개 꼭짓점 → 본 공간 → AABB
        FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
        FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (int32 c = 0; c < 8; ++c)
        {
            const FVector Corner(
                (c & 1) ? BoundsCS.Max.X : BoundsCS.Min.X,
                (c & 2) ? BoundsCS.Max.Y : BoundsCS.Min.Y,
                (c & 4) ? BoundsCS.Max.Z : BoundsCS.Min.Z);

            const FVector P = InverseBindPose.TransformPosition(Corner);

            for (int a = 0; a < 3; ++a) 
            {
                Min[a] = std::min(Min[a], P[a]);
                Max[a] = std::max(Max[a], P[a]);
            }
        }
        const FVector Size = Max - Min;

        //가장 긴 축
        int32 Axis = 2;  // 0=X, 1=Y, 2=Z
        if (Size.X >= Size.Y && Size.X >= Size.Z) Axis = 0;
        else if (Size.Y >= Size.Z)                Axis = 1;
        const int32 A1 = (Axis + 1) % 3, A2 = (Axis + 2) % 3;

        FKSphylElem Capsule;
        Capsule.Radius = std::max(Size[A1], Size[A2]) * 0.5f;
        Capsule.Length = std::max(Size[Axis] - 2.f * Capsule.Radius, 0.f);

        //기본 Z축 → 긴 축
        if (Axis == 0) Capsule.Rotation = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), PI * 0.5f);
        if (Axis == 1) Capsule.Rotation = FQuat::MakeFromAxisAngle(FVector(1, 0, 0), -PI * 0.5f);

        //중심
        Capsule.Center = (Min + Max) * 0.5f;

        OutAxis = Axis;
        return Capsule;
    }
}

UPhysicsAsset* UPhysicsAsset::CreateFromSkeletalMesh(const USkeletalMesh* Mesh)
{
    if (!Mesh || Mesh->GetRenderData().LODRenderData.Num() == 0) return nullptr;
    const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
    const TArray<FBox>& Bounds = Mesh->GetRenderData().LODRenderData[0].BoneBounds;
    if (Bones.Num() == 0 || Bounds.Num() < Bones.Num()) return nullptr;

	UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>();

    //본별 바인드 포즈 기준 SRT 행렬
    TArray<FMatrix> BindCS;
    for (const FBoneInfo& B : Bones) BindCS.Add(B.InverseBindPose.Inverse());

    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        // 바디
        if (!HasVertices(Bounds[i])) continue;

        int32 BoneAxis = 0;
        auto* Body = NewObject<USkeletalBodySetup>(PhysicsAsset);
        Body->BoneName = Bones[i].Name;
        Body->AggGeom.SphylElems.Add(MakeCapsule(Bounds[i], Bones[i].InverseBindPose, BoneAxis));    // BoneBounds로 크기 결정
        PhysicsAsset->SkeletalBodySetups.Add(Body);
        PhysicsAsset->UpdateBodySetupIndexMap();

        // ② 바디가 있는 조상 찾기
        int32 P = Bones[i].ParentIndex;
        while (P != -1 && PhysicsAsset->FindBodyIndex(Bones[P].Name) == -1)
            P = Bones[P].ParentIndex;

        if (P == -1) continue; // 루트 바디에는 관절이 없음

        // ③ 관절 + 프레임
        FQuat Q = FQuat::Identity();                                                        // X
        if (BoneAxis == 1) Q = FQuat::MakeFromAxisAngle(FVector(0, 0, 1), PI * 0.5f);       // Y
        if (BoneAxis == 2) Q = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), -PI * 0.5f);      // Z
        const FTransform QT(Q, FVector::ZeroVector);

        auto* CT = NewObject<UPhysicsConstraintTemplate>(PhysicsAsset);
        FConstraintInstance& CI = CT->DefaultInstance;
        CI.ConstraintBone1 = Bones[i].Name;
        CI.ConstraintBone2 = Bones[P].Name;
        CI.Frame1 = QT;
        CI.Frame2 = FTransform(QT.GetLocalMatrix() * BindCS[i] * BindCS[P].Inverse());
        PhysicsAsset->ConstraintSetup.Add(CT);
    }

    //Body가 없으면 생성 안됨
    if (PhysicsAsset->SkeletalBodySetups.Num() == 0)
    {
        return nullptr;
    }

    return PhysicsAsset;
}
