#pragma once

#include "GameFramework/Pawn.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "WheeledVehicle.generated.h"

class USkeletalMeshComponent;
class UWheeledVehicleMovementComponent;
class USpringArmComponent;
class UCameraComponent;
class UBodySetup;
class UAudioComponent;
class UParticleSystemComponent;
class UTireSkidMarkComponent;

UCLASS()
class AWheeledVehicle : public APawn
{
	GENERATED_BODY()

public:
	AWheeledVehicle();

	void BeginPlay() override;

	virtual void Tick(float DeltaTime) override;

	FVector GetVelocity() const override;

	USkeletalMeshComponent* GetMesh() const { return Mesh; }
	UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	USkeletalMeshComponent* Mesh = nullptr;
	UWheeledVehicleMovementComponent* VehicleMovement = nullptr;
	USpringArmComponent* SpringArmComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;

	UAudioComponent* EngineAudioComponent = nullptr;
	UAudioComponent* SkidAudioComponent = nullptr;
	UTireSkidMarkComponent* SkidMarkComponent = nullptr;

	// Audio Tuning Values
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.1, ClampMax = 3.0, Delta = 0.05))
	float EnginePitchMin = 0.7f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.1, ClampMax = 4.0, Delta = 0.05))
	float EnginePitchMax = 2.2f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 2.0, Delta = 0.05))
	float EngineVolumeMin = 0.4f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 2.0, Delta = 0.05))
	float EngineVolumeMax = 1.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.05, ClampMax = 2.0, Delta = 0.05))
	float SkidSlipThreshold = 0.55f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 100.0, Delta = 0.5))
	float SkidMinSpeedKmh = 25.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 2.0, Delta = 0.05))
	float SkidMaxVolume = 0.8f;

	UPROPERTY(EditAnywhere)
	float EngineInterpSpeed = 15.0f;

	// Tire Smoke Particle Values
	UPROPERTY(EditAnywhere)
	FString TireSmokeParticlePath = "Assets/Particles/P_TireSmoke.particle";

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.05, ClampMax = 2.0, Delta = 0.05))
	float SmokeSlipThreshold = 0.65f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 150.0, Delta = 1.0))
	float SmokeMinSpeedKmh = 35.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 1.0, Delta = 0.01))
	float SmokeSlipHoldTime = 0.08f;

	// Skid Mark Values
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.05, ClampMax = 2.0, Delta = 0.05))
	float SkidMarkSlipThreshold = 0.55f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0, ClampMax = 150.0, Delta = 1.0))
	float SkidMarkMinSpeedKmh = 25.0f;

private:
	UBodySetup* FallbackBodySetup = nullptr;
	TArray<UParticleSystemComponent*> WheelSmokeComponents;
	TArray<float> WheelSmokeSlipTimers;

	void UpdateAudio(float DeltaTime);
	void InitWheelSmokeParticles();
	void UpdateTireSmoke(float DeltaTime);
	void UpdateSkidMarks();
};
