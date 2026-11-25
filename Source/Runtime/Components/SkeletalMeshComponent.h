#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include <functional>
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class USkeletalBodySetup;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;

// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
// Settings > Debug Draw (PIE) > Show Root 값. 에디터가 매 프레임 갱신하고, 런타임 액터("root" 라벨 등)가 읽는다
extern bool GShowSkeletalRootDebug;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;
class USkeletalMeshComponent;

// 물리 결과(fetchResults) 이후에 BlendInPhysics를 부르는 두 번째 틱. 기본 틱은 애니메이션 때문에 PrePhysics에 있다
struct FSkeletalMeshPostPhysicsTickFunction : public FTickFunction
{
	USkeletalMeshComponent* Target = nullptr;
	void ExecuteTick(float DeltaTime) override;
};


UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;
    bool SetAnimBlueprint(const FString& Path);
    bool ReloadAnimBlueprint();
    UAnimGraphInstance* CreateAnimGraph();
    const FString& GetAnimBlueprintPath() const;
    const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	void SetSkeletalMesh(USkeletalMesh* InMesh) override;

	// Ragdoll ON/OFF. ON이면 모든 본 바디를 시뮬레이션하고 PostPhysics에서 바디 → 본으로 되돌린다
	void SetSimulatePhysics(bool bSimulate);
	bool IsSimulatingRagdoll() const { return bRagdollActive; }

	// Show Flag "Physics": 래그돌 바디를 실제 PhysX 위치로 그린다 (시뮬레이션 = 주황, Kinematic = 하늘색)
	void DrawPhysicsDebug(FLineBatcher& LineBatcher);
	// stat physics: 만들어진 바디 수 (OutSimulating = 그중 시뮬레이션 중인 수)와 관절 수
	int32 GetNumPhysicsBodies(int32& OutSimulating) const;
	int32 GetNumPhysicsConstraints() const;

	// Physics Asset 바디마다 (바디 설정, 스케일 없는 월드 포즈, 본 월드 스케일)을 넘긴다. 천 충돌체 수집이 쓴다.
	// 포즈 규칙은 DrawPhysicsDebug와 같다: PhysX 바디가 있으면 PhysX 위치·회전, 없으면(에디터 월드) 본 월드 행렬.
	// 도형(AggGeom)은 본 공간 값이라 받는 쪽이 Scale을 도형에 먼저 곱하고 Pose로 옮겨야 한다
	using FBodyShapeCallback = std::function<void(const USkeletalBodySetup& Setup, const FTransform& Pose, const FVector& Scale)>;
	void ForEachBodyShape(const FBodyShapeCallback& Callback) const;

	void PostEditChangeProperty(const FProperty& Property) override;

	// 테스트용: Details에서 켜고 끈다 (PostEditChangeProperty → SetSimulatePhysics)
	UPROPERTY(EditAnywhere)
	bool bSimulateRagdoll = false;

	UPROPERTY(EditAnywhere)
	bool bUseArticulatedPhysics = true;

	void SetUseArticulatedPhysics(bool bInUse);
	void SetSingleBodySetupOverride(UBodySetup* InBodySetup);
	UBodySetup* GetBodySetup() override;

protected:
	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

//Physics
	// 메시가 바뀌면 Physics Asset도 바뀌므로 바디/조인트를 다시 만든다
	bool ShouldCreatePhysicsState() override;
	void OnCreatePhysicsState() override;
	void OnDestroyPhysicsState() override;

private:
    UPROPERTY()
    FString AnimBlueprintPath;

    TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
    FString AnimBlueprintError;
    bool bBlueprintReloadRequested = false;
    bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	FTransform PendingRootMotion;           // 월드 공간 누적값
	bool bHasPendingRootMotion = false;

	TArray<struct FBodyInstance*>       Bodies;
	TArray<struct FConstraintInstance*> Constraints;
	TArray<int32> BoneToBody;                    // 본 인덱스 → Bodies 인덱스 (바디 없으면 -1)
	bool bRagdollActive = false;                 // 실제로 시뮬레이션 중인지 (bSimulateRagdoll은 요청값)
	bool bAnyBodySimulating = false;             // 시뮬레이션 중인 바디가 하나라도 있는지 (Ragdoll OFF여도 Simulated 바디면 true)
	FSkeletalMeshPostPhysicsTickFunction PostPhysicsTick;
	friend struct FSkeletalMeshPostPhysicsTickFunction;

	UBodySetup* SingleBodySetupOverride = nullptr;
	void InitArticulated(FPhysScene* Scene);
	void TermArticulated();
	void UpdateKinematicBodies();                // Ragdoll OFF: 본 → 바디 (PrePhysics, 애니메이션 직후)
	void BlendInPhysics();                       // Ragdoll ON: 바디 → 본 (PostPhysics)
};
