#pragma once

#include "Components/MeshComponent.h"
#include "ClothComponent.generated.h"

// NvCloth 헤더는 ClothComponent.cpp에서만 include 한다.
namespace nv { namespace cloth { class Solver; class Fabric; class Cloth; } }

// 천 한 장. Fabric(템플릿)과 Cloth(인스턴스)를 짝으로 보관한다.
struct FClothInstance
{
	nv::cloth::Fabric* Fabric = nullptr;   
	nv::cloth::Cloth*  Cloth  = nullptr;   
};

UCLASS()
class UClothComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	UClothComponent();
	virtual ~UClothComponent() override;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime) override;

private:
	nv::cloth::Solver* Solver = nullptr;  
	TArray<FClothInstance> ClothInstances;
};
