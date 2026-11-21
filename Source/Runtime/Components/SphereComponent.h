#pragma once

#include "ShapeComponent.h"
#include "SphereComponent.generated.h"

UCLASS()
class USphereComponent : public UShapeComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	float SphereRadius = 0.5f;

	// UE 4.27은 비균일 scale의 절댓값 중 가장 작은 축을 사용한다
	float GetScaledSphereRadius() const;
	virtual FBox CalcLocalBounds() const override;
	virtual void DrawDebug(FLineBatcher*, const FVector4&) const override;

protected:
	void UpdateBodySetup() override;
};
