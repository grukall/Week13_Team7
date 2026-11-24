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

	// Transient: 리플렉션 자동 저장("Properties")에서 빼고 위 Serialize의 수동 저장만 쓴다 (같은 값이 두 번 저장되지 않게)
	UPROPERTY(EditAnywhere, Transient)
	FConstraintInstance DefaultInstance;
};