#pragma once

#include "Components/MeshComponent.h"
#include "ClothComponent.generated.h"

// NvCloth 헤더는 ClothComponent.cpp에서만 include 한다.
namespace nv { namespace cloth { class Solver; class Fabric; class Cloth; } }

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

private:
	nv::cloth::Solver* Solver = nullptr;  
	TArray<FClothInstance> ClothInstances;		// Component가 소유중인 천 모음, Solver에 들어가있는것과는 별개임
	UPROPERTY(EditAnywhere) int32 DefaultGridRows = 3;
	UPROPERTY(EditAnywhere) int32 DefaultGridCols = 3;
	UPROPERTY(EditAnywhere) float DefaultGridSpacing = 0.1f;
};
