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
    // 자동 생성 기준값(MinBoneSize 등)은 PhysicsAsset.h의 FPhysAssetCreateParams에 있다

    //정점이 하나도 없는 본은 박스가 뒤집혀 있다.
    static bool HasVertices(const FBox& Box)
    {
        return Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
    }

    // 컴포넌트 공간 AABB의 8개 꼭짓점 → 본 공간 → AABB
    void ToBoneSpaceBounds(const FBox& BoundsCS, const FMatrix& InverseBindPose, FVector& OutMin, FVector& OutMax)
    {
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
        OutMin = Min;
        OutMax = Max;
    }

    FKSphylElem MakeCapsule(const FBox& BoundsCS, const FMatrix& InverseBindPose, int32& OutAxis)
    {
        FVector Min, Max;
        ToBoneSpaceBounds(BoundsCS, InverseBindPose, Min, Max);
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

    // GeomType 도형 하나를 Geom에 넣는다. 캡슐이 아닌 도형도 관절 프레임 축은 캡슐과 같은 "가장 긴 축"을 쓴다 (OutAxis)
    void AddFitGeom(FKAggregateGeom& Geom, EPhysAssetFitGeomType GeomType, const FBox& BoundsCS, const FMatrix& InverseBindPose, int32& OutAxis)
    {
        const FKSphylElem Capsule = MakeCapsule(BoundsCS, InverseBindPose, OutAxis);
        if (GeomType == EPhysAssetFitGeomType::Capsule)
        {
            Geom.SphylElems.Add(Capsule);
            return;
        }

        FVector Min, Max;
        ToBoneSpaceBounds(BoundsCS, InverseBindPose, Min, Max);
        const FVector Size = Max - Min;
        const FVector Center = (Min + Max) * 0.5f;
        if (GeomType == EPhysAssetFitGeomType::Box)
        {
            // 예: 본 공간 AABB (-0.05,-0.04,0)~(0.05,0.04,0.3) → X 0.1 · Y 0.08 · Z 0.3, 중심 (0,0,0.15)
            FKBoxElem Box;
            Box.X = Size.X;
            Box.Y = Size.Y;
            Box.Z = Size.Z;
            Box.Center = Center;
            Geom.BoxElems.Add(Box);
        }
        else
        {
            // 위 예라면 반지름 0.15 (가장 긴 변 0.3의 절반). 본을 다 덮는 대신 옆으로는 넉넉하다
            FKSphereElem Sphere;
            Sphere.Radius = std::max(Size.X, std::max(Size.Y, Size.Z)) * 0.5f;
            Sphere.Center = Center;
            Geom.SphereElems.Add(Sphere);
        }
    }
}

namespace
{
    // 관절 자식 프레임: 트위스트 축(X)을 캡슐 길이 축(본 길이 방향)에 맞춘다
    FTransform MakeChildFrame(int32 BoneAxis)
    {
        FQuat Q = FQuat::Identity();                                                        // X
        if (BoneAxis == 1) Q = FQuat::MakeFromAxisAngle(FVector(0, 0, 1), PI * 0.5f);       // Y
        if (BoneAxis == 2) Q = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), -PI * 0.5f);      // Z
        return FTransform(Q, FVector::ZeroVector);
    }

    // 부모 프레임: 바인드 포즈에서 자식 프레임과 같은 월드 위치 · 방향이 되도록 부모 본 공간으로 옮긴다.
    // 조인트 프레임에 스케일은 의미가 없고 저장도 안 하므로 1로 둔다 (불러온 뒤와 같은 상태)
    FTransform MakeParentFrame(const FTransform& ChildFrame, const FMatrix& ChildBindCS, const FMatrix& ParentBindCS)
    {
        FTransform Frame(ChildFrame.GetLocalMatrix() * ChildBindCS * ParentBindCS.Inverse());
        Frame.Scale = FVector::OneVector;
        return Frame;
    }
}

namespace
{
    // 본마다 바인드 포즈 정점의 AABB (컴포넌트 공간). Any는 메시가 미리 만든 BoneBounds와 같다
    TArray<FBox> BuildBoneBounds(const FSkeletalMeshLODRenderData& LOD, int32 NumBones, EPhysAssetFitVertWeight VertWeight)
    {
        if (VertWeight == EPhysAssetFitVertWeight::AnyWeight)
            return LOD.BoneBounds;

        TArray<FBox> Bounds;
        Bounds.SetNum(NumBones);
        for (FBox& Box : Bounds)
            Box = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

        // Dominant: 정점마다 가중치가 가장 큰 본 하나에만 넣는다
        for (int32 V = 0; V < LOD.BindVertices.Num() && V < LOD.SkinWeights.Num(); ++V)
        {
            const FSkinWeight& Weight = LOD.SkinWeights[V];
            int32 Best = -1;
            float BestWeight = 0.0f;
            for (int32 K = 0; K < MaxBoneInfluences; ++K)
            {
                if (Weight.Weight[K] > BestWeight)
                {
                    BestWeight = Weight.Weight[K];
                    Best = Weight.BoneIndex[K];
                }
            }
            if (!Bounds.IsValidIndex(Best))
                continue;
            const FVector& Position = LOD.BindVertices[V].Position;
            FBox& Box = Bounds[Best];
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                Box.Min[Axis] = std::min(Box.Min[Axis], Position[Axis]);
                Box.Max[Axis] = std::max(Box.Max[Axis], Position[Axis]);
            }
        }
        return Bounds;
    }
}

bool UPhysicsAsset::CalcDefaultConstraintFrames(const USkeletalMesh* Mesh, const FString& ChildBone, const FString& ParentBone,
    FTransform& OutFrame1, FTransform& OutFrame2, EPhysAssetFitVertWeight VertWeight)
{
    if (!Mesh || Mesh->GetRenderData().LODRenderData.Num() == 0) return false;
    const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
    const TArray<FBox> Bounds = BuildBoneBounds(Mesh->GetRenderData().LODRenderData[0], Bones.Num(), VertWeight);
    const int32 Child = Mesh->FindBoneIndex(ChildBone);
    const int32 Parent = Mesh->FindBoneIndex(ParentBone);
    if (Child == -1 || Parent == -1 || !Bounds.IsValidIndex(Child)) return false;

    // CreateFromSkeletalMesh와 같은 계산 (캡슐 길이 축 → 자식 프레임 → 부모 프레임)
    int32 BoneAxis = 0;
    if (HasVertices(Bounds[Child]))
        MakeCapsule(Bounds[Child], Bones[Child].InverseBindPose, BoneAxis);
    OutFrame1 = MakeChildFrame(BoneAxis);
    OutFrame2 = MakeParentFrame(OutFrame1, Bones[Child].InverseBindPose.Inverse(), Bones[Parent].InverseBindPose.Inverse());
    return true;
}

bool UPhysicsAsset::CalcBoneGeom(const USkeletalMesh* Mesh, const FString& BoneName, EPhysAssetFitVertWeight VertWeight,
    EPhysAssetFitGeomType GeomType, FKAggregateGeom& OutGeom)
{
    if (!Mesh || Mesh->GetRenderData().LODRenderData.Num() == 0) return false;
    const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
    const int32 Bone = Mesh->FindBoneIndex(BoneName);
    const TArray<FBox> Bounds = BuildBoneBounds(Mesh->GetRenderData().LODRenderData[0], Bones.Num(), VertWeight);
    if (Bone == -1 || !Bounds.IsValidIndex(Bone) || !HasVertices(Bounds[Bone])) return false;

    int32 BoneAxis = 0;
    OutGeom = FKAggregateGeom();
    AddFitGeom(OutGeom, GeomType, Bounds[Bone], Bones[Bone].InverseBindPose, BoneAxis);
    return true;
}

UPhysicsAsset* UPhysicsAsset::CreateFromSkeletalMesh(const USkeletalMesh* Mesh, const FPhysAssetCreateParams& Params)
{
    if (!Mesh || Mesh->GetRenderData().LODRenderData.Num() == 0) return nullptr;
    const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
    const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
    const TArray<FBox> Bounds = BuildBoneBounds(LOD, Bones.Num(), Params.VertWeight);
    if (Bones.Num() == 0 || Bounds.Num() < Bones.Num()) return nullptr;

	UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>();

    //본별 바인드 포즈 기준 SRT 행렬
    TArray<FMatrix> BindCS;
    for (const FBoneInfo& B : Bones) BindCS.Add(B.InverseBindPose.Inverse());

    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        // 바디 (정점이 하나도 없는 본은 크기를 정할 수 없어 bBodyForAll이어도 건너뛴다)
        if (!HasVertices(Bounds[i])) continue;

        int32 BoneAxis = 0;
        // 크기 기준은 도형 종류와 상관없이 캡슐 크기로 본다 (Box · Sphere를 골라도 같은 본에 바디가 생긴다)
        const FKSphylElem Capsule = MakeCapsule(Bounds[i], Bones[i].InverseBindPose, BoneAxis);   // 본 AABB로 크기 결정

        if (!Params.bBodyForAll)
        {
            // 너무 작은 본(손가락 마디 등)은 바디를 만들지 않는다. 몸통과 질량비가 수백 배라 솔버가 관절을 못 지키고 폭발한다
            // (UE FPhysAssetCreateParams::MinBoneSize와 같은 역할). 건너뛴 본은 BlendInPhysics에서 부모를 따라간다
            if (Capsule.Length + 2.f * Capsule.Radius < Params.MinBoneSize) continue;
            // 길이는 충분해도 너무 가는 본(손가락 첫 마디 등)은 사슬 끝에서 오차를 떠안고 계속 떨린다
            if (Capsule.Radius < Params.MinBoneRadius) continue;
        }

        auto* Body = NewObject<USkeletalBodySetup>(PhysicsAsset);
        Body->BoneName = Bones[i].Name;
        AddFitGeom(Body->AggGeom, Params.GeomType, Bounds[i], Bones[i].InverseBindPose, BoneAxis);
        PhysicsAsset->SkeletalBodySetups.Add(Body);
        PhysicsAsset->UpdateBodySetupIndexMap();

        if (!Params.bCreateConstraints) continue;

        // ② 바디가 있는 조상 찾기
        int32 P = Bones[i].ParentIndex;
        while (P != -1 && PhysicsAsset->FindBodyIndex(Bones[P].Name) == -1)
            P = Bones[P].ParentIndex;

        if (P == -1) continue; // 루트 바디에는 관절이 없음

        // ③ 관절 + 프레임
        const FTransform QT = MakeChildFrame(BoneAxis);

        auto* CT = NewObject<UPhysicsConstraintTemplate>(PhysicsAsset);
        FConstraintInstance& CI = CT->DefaultInstance;
        CI.ConstraintBone1 = Bones[i].Name;
        CI.ConstraintBone2 = Bones[P].Name;
        CI.Frame1 = QT;
        CI.Frame2 = MakeParentFrame(QT, BindCS[i], BindCS[P]);
        // 제한 각도는 FConstraintInstance(ConstraintInstance.h)의 기본값을 그대로 쓴다. 관절별 조정은 Physics Asset Editor에서 한다
        CI.Swing1Motion = CI.Swing2Motion = CI.TwistMotion = Params.AngularConstraintMode;
        PhysicsAsset->ConstraintSetup.Add(CT);
    }

    //Body가 없으면 생성 안됨
    if (PhysicsAsset->SkeletalBodySetups.Num() == 0)
    {
        return nullptr;
    }

    return PhysicsAsset;
}
