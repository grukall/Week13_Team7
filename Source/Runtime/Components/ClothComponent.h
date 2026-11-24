#pragma once

#include "Components/MeshComponent.h"
#include "Render/ClothMeshData.h"
#include "ClothComponent.generated.h"

// NvCloth 헤더는 ClothComponent.cpp에서만 include 한다.
namespace nv { namespace cloth { class Solver; class Fabric; class Cloth; } }

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
struct FClothPinSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) bool  bPinRowFirst = false;   // Row 첫줄을 Pin으로
	UPROPERTY(EditAnywhere) bool  bPinRowLast = false;   // Row 끝줄을 Pin으로
	UPROPERTY(EditAnywhere) bool  bPinColFirst = true;    // Col 첫줄을 Pin으로 => 깃발 기본값
	UPROPERTY(EditAnywhere) bool  bPinColLast = false;   // Col 막줄을 Pin으로 
	UPROPERTY(EditAnywhere) bool  bCornersOnly = false;   // 체크된 줄을 양쪽 끝점만 Pin
	UPROPERTY(EditAnywhere) float FreeMaxDistance = 1e4f; // 고정 안 된 점의 반경
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
	// 좌표 공간 (스케일 굽기)
	//  - 렌더 로컬: GPU가 Proxy 행렬(S·R·T)을 곱하는 공간
	//  - 시뮬레이션 공간: 천의 R·T만 벗겨낸 공간. 길이가 월드 단위라서 충돌체 반지름·Pad·두께·중력을 그대로 쓴다
	//  - 시뮬레이션 좌표 = 렌더 로컬 좌표 × S (축별). S는 CreateCloth에서 천 모양에 한 번 굽고, 렌더 직전에 ÷S로 되돌린다
	FVector GetSimScale() const;		// 천의 월드 스케일 (절댓값, 0 나누기 방지 최소값). 음수 스케일(뒤집기)은 지원하지 않는다
	FMatrix GetSimToWorldMatrix() const;	// 시뮬레이션 공간 → 월드 (= Proxy 행렬에서 스케일만 뺀 R·T)
	bool BuildDefaultCloth();			// 기본 격자 천을 만들고 격자 정보(BuiltGridRows/Cols, DefaultGridClothIndex)를 기록
	void DestroyAllCloths();			// 모든 천을 Solver에서 빼고 해제, 렌더 데이터도 비운다 (Solver는 유지)
	void RebuildCloth();				// 스케일이 바뀌었을 때 천을 새 스케일로 다시 만든다

	bool CreateCloth(const FClothBuildInput& RawInput);	// 입력 하나(기본 격자·에셋)로 천 한 장을 만들어 Solver와 ClothInstances에 추가
	void DestroyCloth(FClothInstance& Instance);		// 천 한 장을 Solver에서 빼고 해제
	void ApplyMotionConstraints(FClothInstance& ClothInstance);	// dirty일 때만 점마다 (목표 위치, 반경)을 NvCloth Motion Constraint에 쓴다
	void RefreshPinsFromSettings();		// 현재 PinSettings로 기본 격자 천의 반경을 다시 계산하고 dirty를 켠다 (NvCloth에 쓰는 건 ApplyMotionConstraints)

	// 기본 격자 천을 만들 때 실제로 쓴 값. DefaultGridRows/Cols는 실행 중에 바뀔 수 있어서 점 번호(r * Cols + c) 계산에는 이것을 쓴다
	int32 BuiltGridRows = 0;
	int32 BuiltGridCols = 0;
	int32 DefaultGridClothIndex = -1;	// ClothInstances 중 기본 격자 천의 번호. -1이면 없음
	FVector BuiltScale = FVector(1.0f, 1.0f, 1.0f);	// 지금 천 모양에 구워진 스케일. 렌더 ÷S와 스케일 변경 감지에 쓴다

	nv::cloth::Solver* Solver = nullptr;
	TArray<FClothInstance> ClothInstances;		// Component가 소유중인 천 모음, Solver에 들어가있는것과는 별개임
	TArray<FClothMeshData> ClothMeshDatas;		// Cloth 렌더링용 데이터 배열
	FClothCollisionShapes CollisionShapes;		// Collision 발생시킬 Shape 모음
	UPROPERTY(EditAnywhere) FClothPinSettings PinSettings;
	UPROPERTY(EditAnywhere) float WindTime = 0.0f;
	UPROPERTY(EditAnywhere) float BaseWind = 3.0f;
	UPROPERTY(EditAnywhere) float Friction = 3.0f;
	UPROPERTY(EditAnywhere) bool bEnableGravity = false;
	UPROPERTY(EditAnywhere) bool bEnableWind = true;
	// 자기 충돌 (접힌 천이 자기 자신을 통과하지 않게). 비용이 커서 점 수가 많으면 프레임을 확인할 것
	UPROPERTY(EditAnywhere) bool bEnableSelfCollision = true;
	UPROPERTY(EditAnywhere) float SelfCollisionDistance = 0.06f;	// 점끼리 최소 간격. 격자 간격(0.12)의 절반 정도, 렌더 두께(0.05)보다 커야 겹이 파고들지 않음
	UPROPERTY(EditAnywhere) float SelfCollisionStiffness = 1.0f;	// 0~1	// 끄면 바람 속도 0 (끈 뒤에도 남지 않게 매 Tick 0을 넣음)
	// 기본 격자 천 (에셋이 없을 때). OnRegister에서 한 번 만들므로 값을 바꾸면 다시 등록(재스폰)해야 반영된다
	// Rows·Cols는 "점" 개수. 33×33 점 = 32×32칸 = 사각형 면 1024개(삼각형 2048개)
	UPROPERTY(EditAnywhere) int32 DefaultGridRows = 33;
	UPROPERTY(EditAnywhere) int32 DefaultGridCols = 33;
	UPROPERTY(EditAnywhere) float DefaultGridSpacing = 0.12f;	// 32칸 × 0.12 = 약 3.84 × 3.84 크기 (이전 20점·0.2 간격과 비슷한 크기)
	UPROPERTY(EditAnywhere) float RenderThickness = 0.05f;	// 렌더링 전용 두께 (앞면·뒷면 간격). 0이면 두께 없는 양면. 시뮬레이션에는 영향 없음
};
