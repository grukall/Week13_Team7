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
};

struct FClothInstance			// 천 한장을 정의하는 구조체
{
	nv::cloth::Fabric* Fabric = nullptr;   
	nv::cloth::Cloth*  Cloth  = nullptr;   
	TArray<uint32> Indices;
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

private:
	bool CreateCloth(const FClothBuildInput& Input);	// 입력 하나(기본 격자·에셋)로 천 한 장을 만들어 Solver와 ClothInstances에 추가
	void DestroyCloth(FClothInstance& Instance);		// 천 한 장을 Solver에서 빼고 해제

	nv::cloth::Solver* Solver = nullptr;
	TArray<FClothInstance> ClothInstances;		// Component가 소유중인 천 모음, Solver에 들어가있는것과는 별개임
	TArray<FClothMeshData> ClothMeshDatas;		// Cloth 렌더링용 데이터 배열
	UPROPERTY(EditAnywhere) int32 DefaultGridRows = 3;
	UPROPERTY(EditAnywhere) int32 DefaultGridCols = 3;
	UPROPERTY(EditAnywhere) float DefaultGridSpacing = 0.1f;
};
