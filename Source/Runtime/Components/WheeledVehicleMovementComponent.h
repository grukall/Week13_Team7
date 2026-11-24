#pragma once

#include "Components/PawnMovementComponent.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Transform.h"
#include "WheeledVehicleMovementComponent.generated.h"

namespace physx
{
	class PxVehicleDriveNW;
}

class USkeletalMeshComponent;

USTRUCT()
struct FWheelSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FString BoneName;

	UPROPERTY(EditAnywhere)
	FVector AdditionalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	float Radius = 0.4f;

	UPROPERTY(EditAnywhere)
	float Width = 0.25f;

	UPROPERTY(EditAnywhere)
	float Mass = 20.0f;

	UPROPERTY(EditAnywhere)
	float MaxSteerAngle = 40.0f;

	UPROPERTY(EditAnywhere)
	float SuspensionMaxRaise = 0.15f;

	UPROPERTY(EditAnywhere)
	float SuspensionMaxDrop = 0.15f;

	UPROPERTY(EditAnywhere)
	float SpringStrength = 35000.0f;

	UPROPERTY(EditAnywhere)
	float SpringDamperRate = 4500.0f;

	UPROPERTY(EditAnywhere)
	bool bAffectedByHandbrake = false;

	UPROPERTY(EditAnywhere)
	bool bDriven = true;
};

struct FWheelRuntimeState
{
	int32 BoneIndex = -1;
	FTransform InitialBoneLocalTransform;
	float SuspOffset = 0.0f;
	float SteerAngle = 0.0f;
	float RotationAngle = 0.0f;
	bool bIsInAir = true;

	FQuat InitialCompRotation = FQuat::Identity();
	FQuat ParentCompRotationInv = FQuat::Identity();
	FVector LocalSuspAxis = UpVector;
};

UCLASS()
class UWheeledVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UWheeledVehicleMovementComponent();
	~UWheeledVehicleMovementComponent() override;

	void InitializeComponent() override;
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

	void CreateVehicle();
	void DestroyVehicle();
	void PreTick(float SubstepDeltaTime);
	void UpdateWheelBones();
	bool AutoDetectWheelBones(USkeletalMeshComponent* SkelComp);

	void SetThrottleInput(float Throttle);
	void SetBrakeInput(float Brake);
	void SetSteeringInput(float Steering);
	void SetHandbrakeInput(bool bNewHandbrake);
	float GetForwardSpeed() const;

	UPROPERTY(EditAnywhere)
	float ChassisMass = 1500.0f;

	UPROPERTY(EditAnywhere)
	FVector COMOffset = FVector(0.0f, 0.0f, -0.3f);

	UPROPERTY(EditAnywhere)
	FVector ChassisDimensions = FVector(4.0f, 1.8f, 1.2f);

	UPROPERTY(EditAnywhere)
	bool bAutoDetectWheelBones = true;

	TArray<FWheelSetup> WheelSetups;

protected:
	void OnUnregister() override;

private:
	physx::PxVehicleDriveNW* PVehicle = nullptr;
	TArray<FWheelRuntimeState> WheelStates;

	float RawThrottle = 0.0f;
	float RawBrake = 0.0f;
	float RawSteering = 0.0f;
	bool bRawHandbrake = false;
};
