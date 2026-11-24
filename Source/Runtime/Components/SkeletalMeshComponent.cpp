#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"
#include "UObject/Object.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodyInstance.h"
#include "Physics/SkeletalBodySetup.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Engine/World.h"
#include "ActorComponent.h"
#include "Physics/PhysicsInterface.h"

namespace
{
	// 래그돌 바디 솔버/감쇠 설정 (UE 래그돌도 위치 반복 8회 정도를 쓴다)
	constexpr uint32 RagdollPositionIterations = 8;
	constexpr uint32 RagdollVelocityIterations = 1;
	// 감쇠는 바디마다 USkeletalBodySetup::LinearDamping / AngularDamping (기본 0.05 / 0.1)
	// 질량으로 나눈 운동에너지(≈ 0.5·v²) 기준. PhysX 기본값(0.00005 × 속도 스케일²)은 끝 바디의 미세 떨림 때문에
	// 래그돌 덩어리(island)가 영원히 깨어 있다. 대략 0.1~0.15 m/s 이하로 느려지면 잠들게 한다
	constexpr float RagdollSleepThreshold = 0.01f;
	// Stabilization은 씬에 PxSceneFlag::eENABLE_STABILIZATION이 켜져 있을 때만 동작한다 (현재 꺼져 있음 → Sleep만 효과)
	constexpr float RagdollStabilizationThreshold = 0.005f;
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
    AnimInstance = InInstance;

    if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
    {
        ManagedAnimGraph.reset();
    }

    AnimBlueprintPath.clear();
    bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

    if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
    {
        ReloadAnimBlueprint();
    }

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (bBlueprintReloadRequested)
    {
        bBlueprintReloadRequested = false;
        ReloadAnimBlueprint();
    }

	if (AnimInstance && GetSkeletalMesh())
	{
        bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
        bUpdatingAnimation = false;
	}



	Super::TickComponent(DeltaTime);   // 여기서 CS가 갱신된다

	// 시뮬레이션하지 않는 바디를 애니메이션 포즈로 끌고 간다 (시뮬레이션 중인 바디는 건너뜀)
	UpdateKinematicBodies();
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		HTR_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			HTR_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    UAnimInstance* SavedInstance = AnimInstance;

    if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
    {
        AnimInstance = nullptr;
    }

    Super::Serialize(Record);

    if (Ar.IsSaving())
    {
        AnimInstance = SavedInstance;
    }

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

    if (Ar.IsLoading())
    {
        if (!AnimBlueprintPath.empty())
        {
            ReloadAnimBlueprint();
        }

        RefreshBoneTransformsNow();
    }
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		HTR_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
}
void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	if (GetSkeletalMesh() == InMesh) return;
	Super::SetSkeletalMesh(InMesh);

	RecreatePhysicsState();
}

bool USkeletalMeshComponent::ShouldCreatePhysicsState()
{
	USkeletalMesh* SkeletalMesh = GetSkeletalMesh();
	if (!SkeletalMesh) return false;

	const UPhysicsAsset* PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
	return IsValid(PhysicsAsset);
}

void USkeletalMeshComponent::OnCreatePhysicsState()
{
	//강체와 조인트를 생성하기 전에 본 포즈부터 확정
	RefreshBoneTransformsNow();

	FPhysScene* PhysScene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	InitArticulated(PhysScene);

	// 바디 → 본 동기화는 fetchResults 이후여야 하므로 PostPhysics 틱을 따로 건다
	PostPhysicsTick.Target = this;
	PostPhysicsTick.bCanEverTick = true;
	PostPhysicsTick.TickGroup = ETickingGroup::PostPhysics;
	PostPhysicsTick.RegisterTickFunction(GetWorld()->GetTickTaskManager());

	// 바디마다 시뮬레이션 여부를 정한다. Ragdoll OFF여도 PhysicsType = Simulated 바디는 여기서 켜진다
	// (씬에 Ragdoll ON으로 저장돼 있었으면 모든 Default 바디도 켜진다)
	SetSimulatePhysics(bSimulateRagdoll);

	UActorComponent::OnCreatePhysicsState();
}

void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	PostPhysicsTick.UnRegisterTickFunction();
	bRagdollActive = false;
	bAnyBodySimulating = false;
	TermArticulated();
	UActorComponent::OnDestroyPhysicsState();
}

void FSkeletalMeshPostPhysicsTickFunction::ExecuteTick(float DeltaTime)
{
	if (Target)
		Target->BlendInPhysics();
}

void USkeletalMeshComponent::SetSimulatePhysics(bool bSimulate)
{
	bSimulateRagdoll = bSimulate;
	if (Bodies.IsEmpty())
	{
		// 아직 물리 상태가 없다. OnCreatePhysicsState가 bSimulateRagdoll을 보고 적용한다
		bRagdollActive = false;
		bAnyBodySimulating = false;
		return;
	}

	// eKINEMATIC 플래그를 끄면 PhysX가 중력·관절을 적용하기 시작한다. 켜면 다시 애니메이션이 끌고 다닌다.
	// 바디마다 PhysicsType으로 정한다:
	//   Default   → Ragdoll 요청을 따른다
	//   Kinematic → 항상 애니메이션 (Ragdoll ON이어도)
	//   Simulated → 항상 시뮬레이션 (Ragdoll OFF여도). 관절로 이어진 Kinematic 부모에 매달려 흔들린다
	const UPhysicsAsset* PA = GetSkeletalMesh() ? GetSkeletalMesh()->GetPhysicsAsset() : nullptr;
	bAnyBodySimulating = false;
	for (FBodyInstance* Body : Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
			continue;
		const USkeletalBodySetup* Setup = PA && PA->SkeletalBodySetups.IsValidIndex(Body->InstanceBodyIndex)
			? PA->SkeletalBodySetups[Body->InstanceBodyIndex] : nullptr;
		const EPhysicsType Type = Setup ? Setup->PhysicsType : EPhysicsType::Default;
		const bool bSimulateBody = Type == EPhysicsType::Simulated || (Type == EPhysicsType::Default && bSimulate);
		Body->SetInstanceSimulatePhysics(bSimulateBody);
		bAnyBodySimulating |= bSimulateBody;
	}
	bRagdollActive = bSimulate;
}

void USkeletalMeshComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bSimulateRagdoll")
		SetSimulatePhysics(bSimulateRagdoll);
}

// 애니메이션 포즈의 본 월드 위치로 Kinematic 바디를 끌고 간다 (StartFrame이 서브스텝마다 보간)
// 시뮬레이션하지 않는 바디만 해당된다 (Ragdoll OFF면 Simulated 외 전부, ON이면 Kinematic 바디)
void USkeletalMeshComponent::UpdateKinematicBodies()
{
	for (FBodyInstance* Body : Bodies)
	{
		// 시뮬레이션 중인 바디에 SetBodyTransform을 하면 setGlobalPose로 순간이동하니 건너뛴다
		if (!Body || !Body->IsValidBodyInstance() || Body->bSimulatePhysics)
			continue;
		Body->SetBodyTransform(FTransform(GetBoneWorldMatrix(Body->InstanceBoneIndex)));
	}
}

// 시뮬레이션 중인 바디(Ragdoll ON의 Default 바디, 항상 Simulated 바디)의 PhysX 월드 →
// 컴포넌트 공간(CS) → 부모 기준 Local 로 되돌려 BoneSpaceTransforms에 넣는다
void USkeletalMeshComponent::BlendInPhysics()
{
	// Ragdoll OFF여도 Simulated 바디가 있으면 그 본은 물리 결과로 덮어써야 한다
	if (!bAnyBodySimulating)
		return;
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 NumBones = Bones.Num();
	if (BoneSpaceTransforms.Num() != NumBones || BoneToBody.Num() != NumBones)
		return;

	const FMatrix WorldToComponent = GetWorldMatrix().Inverse();
	const FVector ComponentScale = GetWorldScale3D();

	TArray<FMatrix> CS;
	CS.SetNum(NumBones);
	// 부모가 항상 앞 인덱스라 0번부터 돌면 부모 CS가 먼저 확정된다
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Parent = Bones[i].ParentIndex;
		const int32 BodyIndex = BoneToBody[i];

		// Kinematic 바디(시뮬레이션 안 함)의 본은 애니메이션 값을 그대로 둔다
		if (BodyIndex != -1 && Bodies[BodyIndex]->bSimulatePhysics)
		{
			// 1단계: 월드 → 컴포넌트 공간.
			FTransform BodyWorld = Bodies[BodyIndex]->GetUnrealWorldTransform();
			BodyWorld.Scale = ComponentScale;
			const FMatrix BodyCS = BodyWorld.GetLocalMatrix() * WorldToComponent;

			// 2단계: 컴포넌트 공간 → 부모 기준 Local. 스케일은 애니메이션 값을 유지한다
			FTransform Local(Parent >= 0 ? BodyCS * CS[Parent].Inverse() : BodyCS);
			Local.Scale = BoneSpaceTransforms[i].Scale;
			BoneSpaceTransforms[i] = Local;
		}

		// RefreshBoneTransforms와 같은 식으로 CS를 다시 만들어 자식 계산에 쓴다 (바디 없는 본은 부모를 따라감)
		const FMatrix LocalMatrix = BoneSpaceTransforms[i].GetLocalMatrix();
		CS[i] = Parent >= 0 ? LocalMatrix * CS[Parent] : LocalMatrix;
	}

	// 3단계: CS 재계산 + 스키닝 데이터 전송
	RefreshBoneTransformsNow();
}

//BodyInstance와 ConstraintInstance 생성, 추가
void USkeletalMeshComponent::InitArticulated(FPhysScene* Scene)
{
	UPhysicsAsset* PA = GetSkeletalMesh()->GetPhysicsAsset();
	assert(PA && Scene);

	for (int32 i = 0; i < PA->SkeletalBodySetups.Num(); ++i)
	{
		USkeletalBodySetup* Setup = PA->SkeletalBodySetups[i];
		int32 BoneIdx = Setup ? FindBoneIndex(Setup->BoneName) : -1;
		// 건너뛰어도 자리를 비워 둬야 Bodies[i] == PA 바디 i 가 유지된다 (Constraint가 FindBodyIndex로 접근)
		if (BoneIdx == -1) { Bodies.Add(nullptr); continue; }

		FMatrix WorldBoneMatrix = GetBoneWorldMatrix(BoneIdx);
		FTransform TM(WorldBoneMatrix);

		// TM.Scale = 본의 월드 스케일(본 CS 스케일 × 컴포넌트 스케일). 캡슐 크기는 본 공간 값이라 이 스케일을 곱해야 맞다
		// (예: Armature 본 스케일 100 × 액터 스케일 0.01 = 1). 컴포넌트 스케일만 쓰면 캡슐이 1/100이 되어 관성이 극소 → 폭발

		auto* Body = new FBodyInstance();
		Body->bSimulatePhysics = false;
		Body->ObjectType = ECollisionChannel::ECC_PhysicsBody;
		// Physics Asset 에디터에서 정한 바디 설정 (InitBody 전에 넣어야 질량 · 중력 · 필터에 반영된다)
		Body->CollisionEnabled = Setup->CollisionEnabled;
		Body->bOverrideMass = Setup->bOverrideMass;
		Body->MassInKg = Setup->MassInKg;
		Body->bEnableGravity = Setup->bEnableGravity;
		// Character 캡슐(Pawn) 안에서 시작하므로 Pawn과 충돌하면 켜는 순간 밀려 튕겨 나간다
		Body->CollisionResponses.SetResponse(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);
		Body->InstanceBodyIndex = i;
		Body->InstanceBoneIndex = BoneIdx;

		//강체 생성 (필터 데이터는 InitBody에서 CollisionResponses로 만들어진다)
		Body->InitBody(Setup, TM, this, Scene);
		if (Body->IsValidBodyInstance())
		{
			// 관절이 많은 사슬은 기본 반복(4회)으로는 오차가 남아 쌓인다. 감쇠로 쌓이는 에너지도 조금씩 뺀다 (InitBody가 이미 WaitPhysScenes 함)
			FPhysicsInterface::SetSolverIterationCounts(Body->ActorHandle, RagdollPositionIterations, RagdollVelocityIterations);
			FPhysicsInterface::SetDamping(Body->ActorHandle, Setup->LinearDamping, Setup->AngularDamping);
			FPhysicsInterface::SetSleepThresholds(Body->ActorHandle, RagdollSleepThreshold, RagdollStabilizationThreshold);
		}
		Bodies.Add(Body);
	}
	for (UPhysicsConstraintTemplate* T : PA->ConstraintSetup)
	{
		if (!T) continue;
		// 인덱스 검사를 new보다 먼저 해야 건너뛸 때 누수가 없다
		const int32 Child = PA->FindBodyIndex(T->DefaultInstance.ConstraintBone1);
		const int32 Parent = PA->FindBodyIndex(T->DefaultInstance.ConstraintBone2);
		if (!Bodies.IsValidIndex(Child) || !Bodies.IsValidIndex(Parent)) continue;

		auto* C = new FConstraintInstance(T->DefaultInstance);

		//조인트 생성
		C->InitConstraint(Bodies[Child], Bodies[Parent], Scene);
		Constraints.Add(C);
	}

	// 본 → 바디 표. BlendInPhysics가 본마다 바디를 찾지 않게 한 번 만들어 둔다
	BoneToBody.Init(-1, GetBones().Num());
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		if (Bodies[i] && Bodies[i]->IsValidBodyInstance() && BoneToBody.IsValidIndex(Bodies[i]->InstanceBoneIndex))
			BoneToBody[Bodies[i]->InstanceBoneIndex] = i;
	}
}

void USkeletalMeshComponent::TermArticulated()
{
	for (int32 i = Constraints.Num() - 1; i >= 0; --i)
	{
		Constraints[i]->TermConstraint();
		delete(Constraints[i]);
	}
	Constraints.Empty();

	for (int32 i = Bodies.Num() - 1; i >= 0; --i)
	{
		if (!Bodies[i]) continue;
		Bodies[i]->TermBody();
		delete(Bodies[i]);
	}
	Bodies.Empty();
	BoneToBody.Empty();
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		HTR_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
    AnimBlueprintPath.clear();
    AnimBlueprintError.clear();
    bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
    if (!AnimInstance)
    {
        ResetPose();
        RefreshBoneTransformsNow();
        return;
    }

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
bool GShowSkeletalRootDebug = false;

void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    if (ManagedAnimGraph)
    {
        if (AnimInstance == ManagedAnimGraph.get())
        {
            SetAnimInstance(nullptr);
        }
        else
        {
            ManagedAnimGraph->UninitializeAnimation();
        }
    }
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
    return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
    return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
        return false;
    }

    if (Path.empty())
    {
        const bool bHadBlueprint = !AnimBlueprintPath.empty();
        AnimBlueprintPath.clear();
        AnimBlueprintError.clear();
        bBlueprintReloadRequested = false;

        if (ManagedAnimGraph && bHadBlueprint)
        {
            SetAnimInstance(nullptr);
            ResetPose();
            RefreshBoneTransformsNow();
        }

        return true;
    }

    AnimBlueprintError.clear();
    const FString Key = FAssetUtils::GetAssetKey(Path);
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
    FAnimBlueprintData Data;

    if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
    {
        if (AnimBlueprintError.empty())
        {
            AnimBlueprintError = "Use an .animblueprint asset.";
        }

        return false;
    }

    TUniquePtr<UAnimGraphInstance> Instance;
    AnimBlueprintCompiler::FNodeMap NodeIndices;

    if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
    {
        return false;
    }

    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    AnimBlueprintPath = Key;
    bBlueprintReloadRequested = false;
    AnimBlueprintError.clear();
    RefreshAnimationPose();
    return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
    const FString Path = AnimBlueprintPath;
    bBlueprintReloadRequested = false;

    if (Path.empty())
    {
        return false;
    }

    if (!SetAnimBlueprint(Path))
    {
        HTR_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
        return false;
    }

    return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Create animation graphs outside animation callbacks.";
        return nullptr;
    }

    if (!AnimBlueprintPath.empty())
    {
        AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
        return nullptr;
    }

    AnimBlueprintError.clear();

    if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
    {
        Existing->ResetGraph();
        return Existing;
    }

    TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    return ManagedAnimGraph.get();
}
