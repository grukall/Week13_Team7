#pragma once

#include "Asset/RenderAsset.h"
#include "Physics/PhysicsInterface.h"
#include "PhysicalMaterial.generated.h"

UENUM()
enum class EFrictionCombineMode : uint8
{
	Average, Min, Multiply, Max
};

UCLASS()
class UPhysicalMaterial : public URenderAsset
{
	GENERATED_BODY()

public:
	~UPhysicalMaterial() override;

	UPROPERTY(EditAnywhere)
	float Friction = 0.7f;
	UPROPERTY(EditAnywhere)
	float Restitution = 0.3f;
	UPROPERTY(EditAnywhere)
	float Density = 1.f;
	UPROPERTY(EditAnywhere)
	float RaiseMassToPower = 0.75f;
	UPROPERTY(EditAnywhere)
	bool bOverrideFrictionCombineMode = false;
	UPROPERTY(EditAnywhere)
	EFrictionCombineMode FrictionCombineMode = EFrictionCombineMode::Average;
	UPROPERTY(EditAnywhere)
	bool bOverrideRestitutionCombineMode = false;
	UPROPERTY(EditAnywhere)
	EFrictionCombineMode RestitutionCombineMode = EFrictionCombineMode::Average;

	const FPhysicsMaterialHandle& GetPhysicsMaterial();
	void UpdateMaterial();
	void PostEditChangeProperty(const FProperty& Property) override;

private:
	FPhysicsMaterialHandle MaterialHandle;
};
