#include "EnginePCH.h"
#include "ClothActor.h"

AClothActor::AClothActor()
{
	ClothComponent = CreateDefaultSubobject<UClothComponent>("UClothComponent");
	ClothComponent->SetupAttachment(GetRootComponent());
}
