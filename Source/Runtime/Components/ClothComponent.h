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
};

struct FClothInstance			// 천 한장을 정의하는 구조체
{
	nv::cloth::Fabric* Fabric = nullptr;   
	nv::cloth::Cloth*  Cloth  = nullptr;   
	TArray<uint32> Indices;
};

struct FClothCollisionShapes
{
	TArray<FVector4> Spheres;       
	TArray<uint32>   CapsuleIndices;  
	TArray<FVector4> Planes;        
	TArray<uint32>   ConvexMasks;     
	void Reset() { Spheres.Reset(); CapsuleIndices.Reset(); Planes.Reset(); ConvexMasks.Reset(); }
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

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	// 머티리얼 슬롯 1개. 덮어쓰기(SetMaterial)가 없으면 엔진 기본 머티리얼을 쓴다
	int32 GetNumMaterials() const override { return 1; }
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	// 현재 천 정점 위치(컴포넌트 로컬)를 모두 감싸는 박스. 컬링에 쓰인다
	FBox CalcLocalBounds() const override;

	void GatherCollisionShape(FClothCollisionShapes& CollisionShape, FMatrix WorldToCloth);
	void ApplyClothCollision(nv::cloth::Cloth& Cloth, const FClothCollisionShapes& CollisionShape);

private:
	bool CreateCloth(const FClothBuildInput& Input);	// 입력 하나(기본 격자·에셋)로 천 한 장을 만들어 Solver와 ClothInstances에 추가
	void DestroyCloth(FClothInstance& Instance);		// 천 한 장을 Solver에서 빼고 해제

	nv::cloth::Solver* Solver = nullptr;
	TArray<FClothInstance> ClothInstances;		// Component가 소유중인 천 모음, Solver에 들어가있는것과는 별개임
	TArray<FClothMeshData> ClothMeshDatas;		// Cloth 렌더링용 데이터 배열
	FClothCollisionShapes CollisionShapes;		// Collision 발생시킬 Shape 모음
	UPROPERTY(EditAnywhere) float WindTime = 0.0f;
	UPROPERTY(EditAnywhere) float BaseWind = 3.0f;
	UPROPERTY(EditAnywhere) bool bEnableGravity = false;
	UPROPERTY(EditAnywhere) bool bEnableWind = true;	// 끄면 바람 속도 0 (끈 뒤에도 남지 않게 매 Tick 0을 넣음)
	// 기본 격자 천 (에셋이 없을 때). OnRegister에서 한 번 만들므로 값을 바꾸면 다시 등록(재스폰)해야 반영된다
	UPROPERTY(EditAnywhere) int32 DefaultGridRows = 20;
	UPROPERTY(EditAnywhere) int32 DefaultGridCols = 20;
	UPROPERTY(EditAnywhere) float DefaultGridSpacing = 0.2f;	// 20×20이면 약 3.8 × 3.8 크기
	UPROPERTY(EditAnywhere) float RenderThickness = 0.05f;	// 렌더링 전용 두께 (앞면·뒷면 간격). 0이면 두께 없는 양면. 시뮬레이션에는 영향 없음
};
