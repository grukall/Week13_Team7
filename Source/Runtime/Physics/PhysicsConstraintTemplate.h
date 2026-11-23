#pragma once

#include "UObject/Object.h"
#include "ConstraintInstance.h"
#include "PhysicsConstraintTemplate.generated.h"

//관절 1개의 설계
UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
	GENERATED_BODY()

public:
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	FConstraintInstance DefaultInstance;
};