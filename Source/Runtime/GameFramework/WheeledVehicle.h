#pragma once

#include "GameFramework/Pawn.h"
#include "WheeledVehicle.generated.h"

class USkeletalMeshComponent;
class UWheeledVehicleMovementComponent;
class USpringArmComponent;
class UCameraComponent;
class UBodySetup;

UCLASS()
class AWheeledVehicle : public APawn
{
	GENERATED_BODY()

public:
	AWheeledVehicle();

	void BeginPlay() override;
	FVector GetVelocity() const override;

	USkeletalMeshComponent* GetMesh() const { return Mesh; }
	UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	USkeletalMeshComponent* Mesh = nullptr;
	UWheeledVehicleMovementComponent* VehicleMovement = nullptr;
	USpringArmComponent* SpringArmComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;

private:
	UBodySetup* FallbackBodySetup = nullptr;
};
