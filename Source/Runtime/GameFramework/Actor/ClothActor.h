#pragma once

#include "GameFramework/Actor.h"
#include "Components/ClothComponent.h"
#include "ClothActor.generated.h"

// 에디터 Actor Spawn에서 천 하나를 배치하기 위한 액터. 천 생성·시뮬레이션·렌더링은 UClothComponent가 담당한다.
UCLASS()
class AClothActor : public AActor
{
	GENERATED_BODY()

public:
	AClothActor();

	inline UClothComponent* GetClothComponent() const { return ClothComponent; }

private:
	UClothComponent* ClothComponent;
};
