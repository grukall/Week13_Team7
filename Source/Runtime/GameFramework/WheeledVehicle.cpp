#include "EnginePCH.h"
#include "GameFramework/WheeledVehicle.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/TireSkidMarkComponent.h"
#include "Asset/AssetManager.h"
#include "Physics/BodySetup.h"
#include "UObject/UObjectGlobals.h"
#include <cmath>
#include <string>

AWheeledVehicle::AWheeledVehicle()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh");
	Mesh->bUseArticulatedPhysics = false;
	Mesh->BodyInstance.bSimulatePhysics = true;
	Mesh->BodyInstance.ObjectType = ECollisionChannel::ECC_Vehicle;
	SetRootComponent(Mesh);

	VehicleMovement = CreateDefaultSubobject<UWheeledVehicleMovementComponent>("VehicleMovement");
	VehicleMovement->SetUpdatedComponent(Mesh);

	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(Mesh);
	SpringArmComponent->TargetArmLength = 7.0f;
	SpringArmComponent->TargetOffset = FVector(0.0f, 0.0f, 1.5f);
	SpringArmComponent->SocketOffset = FVector::ZeroVector;
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bInheritPitch = true;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = false;
	SpringArmComponent->bEnableCameraLag = false;
	SpringArmComponent->bEnableCameraRotationLag = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetExternalInputManaged(true);
	CameraComponent->bUsePawnControlRotation = false;

	EngineAudioComponent = CreateDefaultSubobject<UAudioComponent>("EngineAudioComponent");
	EngineAudioComponent->SetupAttachment(Mesh);
	EngineAudioComponent->bLooping = true;
	EngineAudioComponent->SetAutoActivate(false);

	SkidAudioComponent = CreateDefaultSubobject<UAudioComponent>("SkidAudioComponent");
	SkidAudioComponent->SetupAttachment(Mesh);
	SkidAudioComponent->bLooping = true;
	SkidAudioComponent->SetAutoActivate(false);

	SkidMarkComponent = CreateDefaultSubobject<UTireSkidMarkComponent>("SkidMarkComponent");
	SkidMarkComponent->SetupAttachment(Mesh);
}

void AWheeledVehicle::BeginPlay()
{
	if (Mesh && VehicleMovement)
	{
		Mesh->SetUseArticulatedPhysics(false);
		VehicleMovement->SetUpdatedComponent(Mesh);

		// USkeletalMesh::Build()가 자동 생성하는 UPhysicsAsset은 래그돌용 본-로컬 공간 캡슐이므로
		// 컴포넌트 공간 단일 강체인 차량 차체 콜리전에는 BindAABB 기반 전용 차체 박스를 항상 적용한다.
		FallbackBodySetup = NewObject<UBodySetup>(this);
		FKBoxElem BoxElem;
		BoxElem.X = VehicleMovement->ChassisDimensions.X;
		BoxElem.Y = VehicleMovement->ChassisDimensions.Y;
		BoxElem.Z = VehicleMovement->ChassisDimensions.Z;

		// SkeletalMesh가 연결되어 있으면 BindAABB에 맞춰 차체 박스를 구성하되,
		// 바퀴보다 아래로 내려와 바닥과 충돌하지 않도록 지상고(GroundClearance)만큼 밑면을 올린다.
		if (const USkeletalMesh* SkelMesh = Mesh->GetSkeletalMesh())
		{
			const auto& LODs = SkelMesh->GetRenderData().LODRenderData;
			if (!LODs.IsEmpty())
			{
				const FBox& Bounds = LODs[0].BindAABB;
				const FVector Size = Bounds.Max - Bounds.Min;
				if (Size.X > 0.1f && Size.Y > 0.1f && Size.Z > 0.1f)
				{
					const float GroundClearance = std::clamp(Size.Z * 0.25f, 0.2f, 0.5f);
					const float BoxMinZ = Bounds.Min.Z + GroundClearance;
					const float BoxMaxZ = std::max(Bounds.Max.Z, BoxMinZ + 0.3f);

					BoxElem.X = std::max(Size.X * 0.9f, 0.5f);
					BoxElem.Y = std::max(Size.Y * 0.85f, 0.5f);
					BoxElem.Z = BoxMaxZ - BoxMinZ;
					BoxElem.Center = FVector(
						(Bounds.Min.X + Bounds.Max.X) * 0.5f,
						(Bounds.Min.Y + Bounds.Max.Y) * 0.5f,
						(BoxMinZ + BoxMaxZ) * 0.5f);

					VehicleMovement->ChassisDimensions = FVector(BoxElem.X, BoxElem.Y, BoxElem.Z);
				}
			}
		}

		FallbackBodySetup->AggGeom.BoxElems.Add(BoxElem);
		Mesh->SetSingleBodySetupOverride(FallbackBodySetup);
	}

	Super::BeginPlay();

	InitWheelSmokeParticles();
	if (SkidMarkComponent && VehicleMovement)
	{
		SkidMarkComponent->InitializeTracks(VehicleMovement->GetWheelStates().Num());
	}
}

void AWheeledVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateAudio(DeltaTime);
	UpdateTireSmoke(DeltaTime);
	UpdateSkidMarks();
}

FVector AWheeledVehicle::GetVelocity() const
{
	if (!VehicleMovement || !GetRootComponent())
	{
		return FVector::ZeroVector;
	}

	return GetActorRotation().Quaternion().GetForwardVector() * VehicleMovement->GetForwardSpeed();
}

void AWheeledVehicle::UpdateAudio(float DeltaTime)
{
	if (!VehicleMovement)
	{
		return;
	}

	if(EngineAudioComponent && EngineAudioComponent->SoundPath.length() > 0)
	{
		if(!EngineAudioComponent->IsPlaying())
		{
			EngineAudioComponent->Play();
		}
		
		float EngineRPM = VehicleMovement->GetEngineRPM();
		float EngineMaxRPM = VehicleMovement->GetMaxEngineRPM();
		float InvEngineMaxRPM = 1.0f / FMath::Max(EngineMaxRPM, 1.0f);
		
		float TimeRPM = FMath::Clamp(EngineRPM * InvEngineMaxRPM, 0.0f, 1.0f);

		float TargetPitch = FMath::Lerp(EnginePitchMin, EnginePitchMax, TimeRPM);
		float TargetVolume = FMath::Lerp(EngineVolumeMin, EngineVolumeMax, TimeRPM);

		float Alpha = 1 - exp(-EngineInterpSpeed * DeltaTime);

		float NewPitch = FMath::Lerp(EngineAudioComponent->PitchMultiplier, TargetPitch, Alpha);
		float NewVolume = FMath::Lerp(EngineAudioComponent->VolumeMultiplier, TargetVolume, Alpha);

		EngineAudioComponent->SetPitchMultiplier(NewPitch);
		EngineAudioComponent->SetVolumeMultiplier(NewVolume);
	}


	if(SkidAudioComponent && !SkidAudioComponent->SoundPath.empty())
	{
		float MaxSlip = 0.0f;
		for(const FWheelRuntimeState& State : VehicleMovement->GetWheelStates())
		{
			if(State.bIsInAir)
			{
				continue;
			}
			const float WheelSlip = FMath::Max(std::abs(State.LongitudinalSlip), std::abs(State.LateralSlip));
			MaxSlip = FMath::Max(MaxSlip, WheelSlip);
		}

		const float SpeedKmh = std::abs(VehicleMovement->GetSpeedKmh());
		const bool bShouldSkid = (SpeedKmh > SkidMinSpeedKmh) && (MaxSlip > SkidSlipThreshold);

		if(bShouldSkid)
		{
			if(!SkidAudioComponent->IsPlaying())
			{
				SkidAudioComponent->SetVolumeMultiplier(0.0f);
				SkidAudioComponent->Play();
			}

			const float Denominator = FMath::Max(1.0f - SkidSlipThreshold, 0.01f);
			const float Intensity = FMath::Clamp((MaxSlip - SkidSlipThreshold) / Denominator, 0.0f, 1.0f);
			const float TargetVolume = Intensity * SkidMaxVolume;
			 
			const float Alpha = 1.0f - std::exp(-12.0f * DeltaTime);
			const float NewVolume = FMath::Lerp(SkidAudioComponent->VolumeMultiplier, TargetVolume, Alpha);
			SkidAudioComponent->SetVolumeMultiplier(NewVolume);
		}
		else if(SkidAudioComponent->IsPlaying())
		{
			const float Alpha = 1.0f - std::exp(-15.0f * DeltaTime);
			const float NewVolume = FMath::Lerp(SkidAudioComponent->VolumeMultiplier, 0.0f, Alpha);
			if(NewVolume < 0.01f)
			{
				SkidAudioComponent->SetVolumeMultiplier(0.0f);
				SkidAudioComponent->Stop();
			}
			else
			{
				SkidAudioComponent->SetVolumeMultiplier(NewVolume);
			}
		}
	}
}

void AWheeledVehicle::InitWheelSmokeParticles()
{
	WheelSmokeComponents.Reset();
	WheelSmokeSlipTimers.Reset();

	if (!VehicleMovement || !Mesh || TireSmokeParticlePath.empty())
	{
		return;
	}

	UParticleSystem* SmokeTemplate = UAssetManager::LoadParticleSystem(TireSmokeParticlePath);
	if (!SmokeTemplate)
	{
		return;
	}

	const int32 NumWheels = VehicleMovement->GetWheelStates().Num();
	if (NumWheels <= 0)
	{
		return;
	}

	WheelSmokeComponents.SetNum(NumWheels);
	WheelSmokeSlipTimers.SetNum(NumWheels);
	for (int32 i = 0; i < NumWheels; ++i)
	{
		WheelSmokeSlipTimers[i] = 0.0f;

		const FString CompName = "TireSmoke_" + std::to_string(i);
		UParticleSystemComponent* SmokeComp = NewObject<UParticleSystemComponent>(
			this, UParticleSystemComponent::StaticClass(), FName(CompName));
		if (!SmokeComp)
		{
			continue;
		}

		SmokeComp->SetAutoActivate(false);
		SmokeComp->SetupAttachment(Mesh);
		SmokeComp->SetOwner(this);
		Components.Add(SmokeComp);
		SmokeComp->SetTemplate(SmokeTemplate);
		SmokeComp->RegisterComponent();

		WheelSmokeComponents[i] = SmokeComp;
	}
}

void AWheeledVehicle::UpdateTireSmoke(float DeltaTime)
{
	if (!VehicleMovement || WheelSmokeComponents.IsEmpty())
	{
		return;
	}

	const TArray<FWheelRuntimeState>& States = VehicleMovement->GetWheelStates();
	const int32 Count = std::min(States.Num(), WheelSmokeComponents.Num());
	if (WheelSmokeSlipTimers.Num() < Count)
	{
		WheelSmokeSlipTimers.SetNum(Count);
	}

	const float SpeedKmh = std::abs(VehicleMovement->GetSpeedKmh());

	for (int32 i = 0; i < Count; ++i)
	{
		UParticleSystemComponent* SmokeComp = WheelSmokeComponents[i];
		if (!SmokeComp)
		{
			continue;
		}

		const FWheelRuntimeState& State = States[i];
		const float WheelSlip = FMath::Max(std::abs(State.LongitudinalSlip), std::abs(State.LateralSlip));
		const bool bSlippingFast = (!State.bIsInAir) && (SpeedKmh >= SmokeMinSpeedKmh) && (WheelSlip > SmokeSlipThreshold);

		if (bSlippingFast)
		{
			WheelSmokeSlipTimers[i] += DeltaTime;
		}
		else
		{
			WheelSmokeSlipTimers[i] = 0.0f;
		}

		const bool bShouldSmoke = bSlippingFast && (WheelSmokeSlipTimers[i] >= SmokeSlipHoldTime);
		if (bShouldSmoke)
		{
			SmokeComp->SetWorldLocation(State.ContactPoint, false);
			if (!SmokeComp->IsActive())
			{
				SmokeComp->Activate(false);
			}
		}
		else if (SmokeComp->IsActive())
		{
			SmokeComp->Deactivate();
		}
	}
}

void AWheeledVehicle::UpdateSkidMarks()
{
	if (!VehicleMovement || !SkidMarkComponent)
	{
		return;
	}

	const TArray<FWheelRuntimeState>& States = VehicleMovement->GetWheelStates();
	const TArray<FWheelSetup>& Setups = VehicleMovement->WheelSetups;
	const int32 Count = States.Num();
	const float SpeedKmh = std::abs(VehicleMovement->GetSpeedKmh());
	const float Denominator = FMath::Max(1.0f - SkidMarkSlipThreshold, 0.01f);

	for (int32 i = 0; i < Count; ++i)
	{
		const FWheelRuntimeState& State = States[i];
		const float WheelWidth = Setups.IsValidIndex(i) ? Setups[i].Width : 0.25f;
		const float WheelSlip = FMath::Max(std::abs(State.LongitudinalSlip), std::abs(State.LateralSlip));
		const bool bSkidding = (!State.bIsInAir) && (SpeedKmh >= SkidMarkMinSpeedKmh) && (WheelSlip > SkidMarkSlipThreshold);
		const float Intensity = bSkidding
			? FMath::Clamp((WheelSlip - SkidMarkSlipThreshold) / Denominator, 0.2f, 1.0f)
			: 0.0f;

		SkidMarkComponent->UpdateWheelTrack(
			i,
			bSkidding,
			State.ContactPoint,
			State.ContactNormal,
			WheelWidth,
			Intensity);
	}
}
