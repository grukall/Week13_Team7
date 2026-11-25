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
class UWorld;
class FLineBatcher;

void DrawVehicleDebug(UWorld* World, FLineBatcher* LineBatcher);
extern bool GShowVehicleDebug;

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

	FVector WheelLocalPos = FVector::ZeroVector;
	float LongitudinalSlip = 0.0f;
	float LateralSlip = 0.0f;
	FVector ContactPoint = FVector::ZeroVector;
	FVector ContactNormal = UpVector;
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
	void DrawDebug(FLineBatcher* LineBatcher) const;

	void SetThrottleInput(float Throttle);
	void SetBrakeInput(float Brake);
	void SetSteeringInput(float Steering);
	void SetHandbrakeInput(bool bNewHandbrake);
	float GetForwardSpeed() const;
	float GetSpeedKmh() const;
	float GetEngineRPM() const;
	float GetMaxEngineRPM() const;
	int32 GetCurrentGear() const;
	const TArray<FWheelRuntimeState>& GetWheelStates() const;

	UPROPERTY(EditAnywhere, meta=(ClampMin=100.0, ClampMax=50000.0, Delta=10.0))
	float ChassisMass = 1585.0f;

	UPROPERTY(EditAnywhere)
	FVector COMOffset = FVector(-0.22f, 0.0f, -0.25f);

	UPROPERTY(EditAnywhere, meta=(ClampMin=-1.0, ClampMax=1.0, Delta=0.05))
	float COMLongitudinalBias = -0.22f;

	UPROPERTY(EditAnywhere)
	FVector ChassisDimensions = FVector(4.519f, 1.852f, 1.300f);

	UPROPERTY(EditAnywhere)
	bool bAutoDetectWheelBones = true;

	UPROPERTY(EditAnywhere)
	bool bAllWheelDrive = false;

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

public:
	// PxVehicleDriveSimDataNW mapping property (2019 Porsche 911 Carrera S Cabriolet 992.1)
	UPROPERTY(EditAnywhere, meta=(ClampMin=10.0, ClampMax=10000.0, Delta=10.0))
	float PeakTorque = 530.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=100.0, ClampMax=2000.0, Delta=10.0))
	float MaxEngineOmega = 785.4f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.05, ClampMax=2.0, Delta=0.05))
	float GearSwitchTime = 0.12f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1.0, ClampMax=100.0, Delta=1.0))
	float ClutchStrength = 22.0f;

	// PxVehicleWheelsSimData mapping property
	UPROPERTY(EditAnywhere, meta=(ClampMin=1.0, ClampMax=500.0, Delta=1.0))
	float DefaultWheelMass = 22.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1000.0, ClampMax=200000.0, Delta=500.0))
	float DefaultSpringStrength = 55000.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=100.0, ClampMax=50000.0, Delta=100.0))
	float DefaultSpringDamperRate = 6200.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, ClampMax=1.0, Delta=0.01))
	float DefaultSuspensionMaxRaise = 0.10f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, ClampMax=1.0, Delta=0.01))
	float DefaultSuspensionMaxDrop = 0.12f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=70.0, Delta=1.0))
	float DefaultMaxSteerAngle = 35.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=20000.0, Delta=50.0))
	float MaxBrakeTorque = 2400.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=20000.0, Delta=100.0))
	float MaxHandBrakeTorque = 4500.0f;

	// PxVehicleTireData mapping property
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=10.0, Delta=0.1))
	float TireLateralStiffnessX = 2.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1.0, ClampMax=100.0, Delta=0.5))
	float TireLateralStiffnessY = 24.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=10.0, ClampMax=10000.0, Delta=50.0))
	float TireLongitudinalStiffness = 1500.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=5.0, Delta=0.05))
	float TireFrictionPeak = 1.25f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.1, ClampMax=5.0, Delta=0.05))
	float TireFrictionSlip = 1.05f;

	// High-Speed Stability & Drift Assist
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.5, ClampMax=2.0, Delta=0.05))
	float RearTireGripMultiplier = 1.12f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=5.0, Delta=0.05))
	float AeroDragCoeff = 0.37f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=5.0, Delta=0.05))
	float AeroDownforceCoeff = 0.45f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=3.0, Delta=0.1))
	float DriftStabilityStrength = 1.0f;
};
