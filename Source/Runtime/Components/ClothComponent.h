#pragma once

#include "Components/MeshComponent.h"
#include "Render/ClothMeshData.h"
#include "ClothComponent.generated.h"

// NvCloth 헤더는 ClothComponent.cpp에서만 include 한다.
namespace nv { namespace cloth { class Factory; class Solver; class Fabric; class Cloth; } }

// 천 한 장을 만드는 데 필요한 원본 정보. 격자·메시 에셋 등 원본이 무엇이든 이 형태로 채움
struct FClothBuildInput
{
	TArray<FVector> Positions;	// 점 위치 (컴포넌트 로컬 좌표)
	TArray<uint32>  Indices;	// 삼각형 점 번호, 3개씩 (파티클 번호 기준)
	TArray<float>   InvMasses;	// 점 역질량. 0 = 고정점, Positions와 개수가 같아야 한다
	TArray<FVector2> UVs;
	TArray<float> MaxDistances;   // 점마다 1개. 0 = 고정, 클수록 자유. Positions와 개수가 같아야 한다
};

struct FClothInstance			// 천 한장을 정의하는 구조체
{
	nv::cloth::Fabric* Fabric = nullptr;   
	nv::cloth::Cloth*  Cloth  = nullptr;   
	TArray<uint32> Indices;
	TArray<FVector> MotionTargets;	// Motion Constraint를 위한 점마다의 목표 위치
	TArray<float>   MaxDistances;	// 점마다의 MaxDistancez. 0 = 고정, 클수록 자유
	bool bMotionDirty = true;		// Motion 점 변경시에 체크를 위한 DirtyFlag
	FClothBuildInput SourceInput;	// 이 천을 만든 원본 입력 (렌더 로컬, 스케일 굽기 전). 스케일이 바뀌면 이걸로 다시 만든다
};

struct FClothCollisionShapes	// Collision 처리를 위해 Collision이 발생한 Shape들 모아놓는 배열
{
	TArray<FVector4> Spheres;       
	TArray<uint32>   CapsuleIndices;  
	TArray<FVector4> Planes;        
	TArray<uint32>   ConvexMasks;     
	void Reset() { Spheres.Reset(); CapsuleIndices.Reset(); Planes.Reset(); ConvexMasks.Reset(); }
};

USTRUCT()
struct FClothSimSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) bool bEnableGravity = false;
	UPROPERTY(EditAnywhere) bool bEnableWind = true;	
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1)) float WindStrength = 3.0f;	
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0, Delta = 0.05)) float Friction = 3.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01)) float LinearInertia = 1.0f;		// 이동 가속·감속 → 뒤처짐
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01)) float AngularInertia = 1.0f;		// 회전 가속·감속
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01)) float CentrifugalInertia = 1.0f;	// 회전 중 바깥으로 벌어짐

	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1)) float TeleportDistance = 2.0f;
};

USTRUCT()
struct FClothSelfCollisionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) bool bEnabled = true;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.005)) float Distance = 0.06f;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01)) float Stiffness = 1.0f;
};

USTRUCT()
struct FClothGridSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta=(ClampMin=2, ClampMax=128)) int32 Rows = 33;
	UPROPERTY(EditAnywhere, meta=(ClampMin=2, ClampMax=128)) int32 Cols = 33;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.001, Delta=0.005)) float Spacing = 0.12f;
};


USTRUCT()
struct FClothPinSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) bool  bPinRowFirst = false;   // Row 첫줄을 Pin으로
	UPROPERTY(EditAnywhere) bool  bPinRowLast = false;   // Row 끝줄을 Pin으로
	UPROPERTY(EditAnywhere) bool  bPinColFirst = true;    // Col 첫줄을 Pin으로 => 깃발 기본값
	UPROPERTY(EditAnywhere) bool  bPinColLast = false;   // Col 막줄을 Pin으로
	UPROPERTY(EditAnywhere) bool  bCornersOnly = false;   // 체크된 줄을 양쪽 끝점만 Pin
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=1)) float FreeMaxDistance = 1e4f; // 고정 안 된 점의 반경
};

UCLASS()
class UClothComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	UClothComponent();
	~UClothComponent() override;

	void OnRegister() override;
	void OnUnregister() override;
	void TickComponent(float DeltaTime) override;

	// 천 전용 Proxy(FClothSceneProxy)를 만든다. 그림자 패스는 HasDynamicMeshElements가 true인 Proxy만 동적 Caster로 받는다
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	// 머티리얼 슬롯 1개. 덮어쓰기(SetMaterial)가 없으면 엔진 기본 머티리얼을 쓴다
	int32 GetNumMaterials() const override { return 1; }
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	// 현재 천 정점 위치(컴포넌트 로컬)를 모두 감싸는 박스. 컬링에 쓰인다
	FBox CalcLocalBounds() const override;

	void GatherCollisionShape(FClothCollisionShapes& CollisionShape, FMatrix WorldToCloth);
	void ApplyClothCollision(nv::cloth::Cloth& Cloth, const FClothCollisionShapes& CollisionShape);

	// 디테일 패널에서 값이 바뀌면 불린다. 구조체 멤버가 바뀌어도 바깥 이름("PinSettings")으로 온다 (DetailsPanel.cpp DrawProperty)
	void PostEditChangeProperty(const FProperty& Property) override;

	// 코드에서 고정 설정을 바꾸는 입구. 다음 Tick에 반영된다
	void SetPinSettings(const FClothPinSettings& NewSettings);
	const FClothPinSettings& GetPinSettings() const { return PinSettings; }

	bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;

private:
	FVector GetSimScale() const;		// 천의 월드 스케일 (절댓값, 0 나누기 방지 최소값). 음수 스케일(뒤집기)은 지원하지 않는다
	FMatrix GetSimToWorldMatrix() const;	// 시뮬레이션 공간 → 월드 (= Proxy 행렬에서 스케일만 뺀 R·T)
	bool BuildDefaultCloth();			// 기본 격자 천을 만들고 격자 정보(BuiltGridRows/Cols, DefaultGridClothIndex)를 기록
	void DestroyAllCloths();			// 모든 천을 Solver에서 빼고 해제, 렌더 데이터도 비운다 (Solver는 유지)

	void RebuildCloth(bool bRecreateSolver = false, bool bRegenerateGrid = false);
	bool CreateSolver();				// 전역 설정(NvClothManager::GetUseCUDA)에 맞는 Factory로 Solver를 만든다. CUDA가 없거나 실패하면 CPU로 대체. SimFactory·Solver를 채운다
	void DestroySolver();				// 천이 모두 빠진 뒤에 부른다

	bool CreateCloth(const FClothBuildInput& RawInput);	// 입력 하나(기본 격자·에셋)로 천 한 장을 만들어 Solver와 ClothInstances에 추가
	void DestroyCloth(FClothInstance& Instance);		// 천 한 장을 Solver에서 빼고 해제
	void ApplyMotionConstraints(FClothInstance& ClothInstance);	// dirty일 때만 점마다 (목표 위치, 반경)을 NvCloth Motion Constraint에 쓴다
	void RefreshPinsFromSettings();		// 현재 PinSettings로 기본 격자 천의 반경을 다시 계산하고 dirty를 켠다 (NvCloth에 쓰는 건 ApplyMotionConstraints)

	// 기본 격자 천을 만들 때 실제로 쓴 값. GridSettings는 실행 중에 바뀔 수 있어서 점 번호(r * Cols + c) 계산에는 이것을 쓴다
	int32 BuiltGridRows = 0;
	int32 BuiltGridCols = 0;
	int32 DefaultGridClothIndex = -1;	// ClothInstances 중 기본 격자 천의 번호. -1이면 없음
	FVector BuiltScale = FVector(1.0f, 1.0f, 1.0f);	// 지금 천 모양에 구워진 스케일. 렌더 ÷S와 스케일 변경 감지에 쓴다

	// Solver·Fabric·Cloth를 만든 Factory. 서로 다른 Factory의 객체는 섞을 수 없으므로 셋 모두 이것으로 만든다 (NvClothManager 소유)
	nv::cloth::Factory* SimFactory = nullptr;
	nv::cloth::Solver* Solver = nullptr;
	// Solver를 만들 때의 NvClothManager::GetBackendSerial(). 전역 CPU/CUDA 설정(Settings 패널)이 바뀌면 달라지고,
	// 다음 Tick 시작(simulate 전)에 RebuildCloth(true)로 새 Factory에 다시 만든다. CUDA가 실패해 CPU로 대체돼도 번호는 맞추므로 매 프레임 재생성하지 않는다
	uint32 BuiltBackendSerial = 0;
	TArray<FClothInstance> ClothInstances;		// Component가 소유중인 천 모음, Solver에 들어가있는것과는 별개임
	TArray<FClothMeshData> ClothMeshDatas;		// Cloth 렌더링용 데이터 배열
	FClothCollisionShapes CollisionShapes;		// Collision 발생시킬 Shape 모음
	bool bGridDirty = false;		// GridSettings가 바뀌었다. 다음 Tick 시작(simulate 전)에 기본 격자 천을 다시 만든다
	float WindTime = 0.0f;			// 돌풍 계산용 누적 시간 (실행 중 상태라 편집·저장하지 않는다)

	UPROPERTY(EditAnywhere) FClothSimSettings SimSettings;
	UPROPERTY(EditAnywhere) FClothSelfCollisionSettings SelfCollision;
	UPROPERTY(EditAnywhere) FClothPinSettings PinSettings;
	UPROPERTY(EditAnywhere) FClothGridSettings GridSettings;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.005)) float RenderThickness = 0.05f;
};
