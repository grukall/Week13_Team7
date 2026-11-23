#include "EnginePCH.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXVehicleManager.h"
#include <PxPhysicsAPI.h>

UWheeledVehicleMovementComponent::UWheeledVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Default 4-Wheels (0: FL, 1: FR, 2: RL, 3: RR, +X Forward / +Y Right / +Z Up)
	WheelSetups.SetNum(4);

	WheelSetups[0].AdditionalOffset = { 1.4f, -0.85f, -0.3f };
	WheelSetups[0].MaxSteerAngle = 40.0f;
	WheelSetups[0].bAffectedByHandbrake = false;
	WheelSetups[0].bDriven = true;

	WheelSetups[1].AdditionalOffset = { 1.4f, 0.85f, -0.3f };
	WheelSetups[1].MaxSteerAngle = 40.0f;
	WheelSetups[1].bAffectedByHandbrake = false;
	WheelSetups[1].bDriven = true;

	WheelSetups[2].AdditionalOffset = { -1.4f, -0.85f, -0.3f };
	WheelSetups[2].MaxSteerAngle = 0.0f;
	WheelSetups[2].bAffectedByHandbrake = true;
	WheelSetups[2].bDriven = true;

	WheelSetups[3].AdditionalOffset = { -1.4f, 0.85f, -0.3f };
	WheelSetups[3].MaxSteerAngle = 0.0f;
	WheelSetups[3].bAffectedByHandbrake = true;
	WheelSetups[3].bDriven = true;
}

UWheeledVehicleMovementComponent::~UWheeledVehicleMovementComponent()
{
	DestroyVehicle();
}

void UWheeledVehicleMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (!UpdatedComponent && GetOwner())
	{
		UpdatedComponent = GetOwner()->GetRootComponent();
	}
}

void UWheeledVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	CreateVehicle();
}

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (PVehicle)
	{
		UpdateWheelBones();
	}
}

void UWheeledVehicleMovementComponent::OnUnregister()
{
	DestroyVehicle();

	Super::OnUnregister();
}

void UWheeledVehicleMovementComponent::CreateVehicle()
{
	// Safety

	if(PVehicle)
	{
		DestroyVehicle();
	}

	const uint32 NumWheels = static_cast<uint32>(WheelSetups.Num());

	if(NumWheels == 0 || NumWheels > PX_MAX_NB_WHEELS)
	{
		return;
	}

	if (!UpdatedComponent && GetOwner())
	{
		UpdatedComponent = GetOwner()->GetRootComponent();
	}

	USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(UpdatedComponent);
	if (SkelComp == nullptr)
	{
		return;
	}

	if (!SkelComp->BodyInstance.IsValidBodyInstance())
	{
		return;
	}

	// Get PxRigidDynamic
	FPhysScene* PhysScene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	physx::PxPhysics* Physics = GEngine ? GEngine->GetPhysXRuntime().Physics : nullptr;
	if (!PhysScene || !Physics)
	{
		return;
	}

	physx::PxRigidDynamic* RigidDynamic = SkelComp->BodyInstance.ActorHandle.SyncActor->is<physx::PxRigidDynamic>();
	if (RigidDynamic == nullptr)
	{
		return;
	}

	PhysScene->WaitPhysScenes();

	SkelComp->BodyInstance.ObjectType = ECollisionChannel::ECC_Vehicle;
	SkelComp->BodyInstance.SetInstanceSimulatePhysics(true);
	SkelComp->BodyInstance.UpdatePhysicsFilterData();

	// Vehicle mass, Moment of Inertia, Center of Mass
	const float X = ChassisDimensions.X;
	const float Y = ChassisDimensions.Y;
	const float Z = ChassisDimensions.Z;
	const physx::PxVec3 ChassisMOI(
		(Y * Y + Z * Z) * ChassisMass / 12.0f,
		(X * X + Z * Z) * ChassisMass / 12.0f,
		(X * X + Y * Y) * ChassisMass / 12.0f
	);
	RigidDynamic->setMass(ChassisMass);
	RigidDynamic->setMassSpaceInertiaTensor(ChassisMOI);
	RigidDynamic->setCMassLocalPose(physx::PxTransform(physx::PxVec3(COMOffset.X, COMOffset.Y, COMOffset.Z)));

	// Set wheel bone position and initialize WheelStates
	WheelStates.Reset();
	WheelStates.SetNum(static_cast<int32>(NumWheels));
	TArray<physx::PxVec3> WheelCentreCMOffsets;
	WheelCentreCMOffsets.SetNum(static_cast<int32>(NumWheels));

	for (uint32 i = 0; i < NumWheels; ++i)
	{
		const int32 Idx = static_cast<int32>(i);
		const FWheelSetup& Setup = WheelSetups[Idx];
		FVector WheelPos = Setup.AdditionalOffset;

		if (!Setup.BoneName.empty())
		{
			const int32 BoneIndex = SkelComp->FindBoneIndex(Setup.BoneName);
			if (BoneIndex != -1)
			{
				WheelStates[Idx].BoneIndex = BoneIndex;
				WheelStates[Idx].InitialBoneLocalTransform = SkelComp->GetBoneLocalTransform(BoneIndex);
				const FVector4 BoneOrigin = SkelComp->GetBoneComponentMatrix(BoneIndex).GetOrigin();
				WheelPos = FVector(BoneOrigin.X, BoneOrigin.Y, BoneOrigin.Z) + Setup.AdditionalOffset;
			}
		}

		WheelCentreCMOffsets[Idx] = physx::PxVec3(
			WheelPos.X - COMOffset.X,
			WheelPos.Y - COMOffset.Y,
			WheelPos.Z - COMOffset.Z);
	}

	// Set PxVehicleWheelsSimData
	physx::PxVehicleWheelsSimData* WheelsSimData = physx::PxVehicleWheelsSimData::allocate(NumWheels);

	TArray<physx::PxF32> SprungMasses;
	SprungMasses.SetNum(static_cast<int32>(NumWheels));
	if(NumWheels >= 3)
	{
		physx::PxVehicleComputeSprungMasses(
			NumWheels,
			WheelCentreCMOffsets.GetData(),
			physx::PxVec3(0.0f, 0.0f, 0.0f),
			ChassisMass,
			2,
			SprungMasses.GetData());
	}
	else
	{
		const float EqualMass = ChassisMass / static_cast<float>(NumWheels);
		for(uint32 i = 0; i < NumWheels; i++)
		{
			SprungMasses[static_cast<int32>(i)] = EqualMass;
		}
	}

	physx::PxFilterData QueryFilter;
	QueryFilter.word0 = GetOwner() ? GetOwner()->GetUUID() : 0;

	for(uint32 i = 0; i < NumWheels; ++i)
	{
		const int32 Idx = static_cast<int32>(i);
		const FWheelSetup& Setup = WheelSetups[Idx];
		const physx::PxVec3& WheelCMOffset = WheelCentreCMOffsets[Idx];

		physx::PxVehicleWheelData WheelData;
		WheelData.mMass = Setup.Mass;
		WheelData.mRadius = Setup.Radius;
		WheelData.mWidth = Setup.Width;
		WheelData.mMOI = 0.5f * Setup.Mass * Setup.Radius * Setup.Radius; // 원기둥 관성 모멘트: 0.5 * m * r^2
		WheelData.mMaxBrakeTorque = 1500.0f;                              // 풋브레이크 기본 토크 (N·m)
		WheelData.mMaxHandBrakeTorque = Setup.bAffectedByHandbrake ? 4000.0f : 0.0f; // 휠 잠금용 핸드브레이크 토크 (N·m)
		WheelData.mMaxSteer = FMath::DegreesToRadians(Setup.MaxSteerAngle);

		physx::PxVehicleSuspensionData SuspData;
		SuspData.mMaxCompression = Setup.SuspensionMaxRaise;
		SuspData.mMaxDroop = Setup.SuspensionMaxDrop;
		SuspData.mSpringStrength = Setup.SpringStrength;
		SuspData.mSpringDamperRate = Setup.SpringDamperRate;
		SuspData.mSprungMass = SprungMasses[Idx];

		physx::PxVehicleTireData TireData;
		TireData.mType = 0; // UpdateTireFrictionTable()에 등록한 0번 타이어 타입과 매칭

		// 힘 작용점 Z 오프셋(-0.3m): 무게중심보다 약간 아래에 두어 롤링을 유지하면서 전복 억제
		const physx::PxVec3 ForceAppPointOffset(WheelCMOffset.x, WheelCMOffset.y, -0.3f);

		WheelsSimData->setWheelData(i, WheelData);
		WheelsSimData->setSuspensionData(i, SuspData);
		WheelsSimData->setTireData(i, TireData);
		WheelsSimData->setSuspTravelDirection(i, physx::PxVec3(0.0f, 0.0f, -1.0f)); // Z-Up 기준 로컬 하방
		WheelsSimData->setWheelCentreOffset(i, WheelCMOffset);
		WheelsSimData->setSuspForceAppPointOffset(i, ForceAppPointOffset);
		WheelsSimData->setTireForceAppPointOffset(i, ForceAppPointOffset);
		WheelsSimData->setWheelShapeMapping(i, -1); // -1: 액터에 별도 휠 PxShape 미부착
		WheelsSimData->setSceneQueryFilterData(i, QueryFilter);
	}

	WheelsSimData->setSubStepCount(5.0f, 3, 1);   // 5 m/s 미만 저속에서 3회 서브스텝(떨림 방지), 고속 1회
	WheelsSimData->setMinLongSlipDenominator(4.0f); // 정지 근처 종방향 슬립 분모 0 발산 방지 하한 (m/s)

	// Set PxVehicleDriveSimDataNW
	physx::PxVehicleDriveSimDataNW DriveSimData;

	physx::PxVehicleDifferentialNWData Diff;
	bool bHasDrivenWheel = false;
	for (uint32 i = 0; i < NumWheels; ++i)
	{
		const bool bIsDriven = WheelSetups[static_cast<int32>(i)].bDriven;
		Diff.setDrivenWheel(i, bIsDriven);
		bHasDrivenWheel |= bIsDriven;
	}
	if (!bHasDrivenWheel)
	{
		Diff.setDrivenWheel(0, true);
	}
	DriveSimData.setDiffData(Diff);

	physx::PxVehicleEngineData EngineData;
	EngineData.mPeakTorque = 500.0f;
	EngineData.mMaxOmega = 600.0f;
	DriveSimData.setEngineData(EngineData);

	physx::PxVehicleGearsData GearsData;
	GearsData.mSwitchTime = 0.5f;
	DriveSimData.setGearsData(GearsData);

	physx::PxVehicleClutchData ClutchData;
	ClutchData.mStrength = 10.0f;
	DriveSimData.setClutchData(ClutchData);

	// PxVehicleDriveNW
	PVehicle = physx::PxVehicleDriveNW::allocate(NumWheels);
	PVehicle->setup(Physics, RigidDynamic, *WheelsSimData, DriveSimData, NumWheels);
	WheelsSimData->free();
	PVehicle->setToRestState();
	PVehicle->mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eFIRST);
	PVehicle->mDriveDynData.setUseAutoGears(true);

	if (auto* VM = PhysScene->GetVehicleManager())
	{
		VM->AddVehicle(this, PVehicle);
	}
}

void UWheeledVehicleMovementComponent::DestroyVehicle()
{
	if (!PVehicle)
	{
		return;
	}

	if (FPhysScene* PhysScene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr)
	{
		PhysScene->WaitPhysScenes();
		if (FPhysXVehicleManager* VM = PhysScene->GetVehicleManager())
		{
			VM->RemoveVehicle(this, PVehicle);
		}
	}

	PVehicle->free();
	PVehicle = nullptr;
	WheelStates.Reset();

	RawThrottle = 0.0f;
	RawBrake = 0.0f;
	RawSteering = 0.0f;
	bRawHandbrake = false;
}

void UWheeledVehicleMovementComponent::SetThrottleInput(float Throttle)
{
	RawThrottle = std::clamp(Throttle, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetBrakeInput(float Brake)
{
	RawBrake = std::clamp(Brake, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetSteeringInput(float Steering)
{
	RawSteering = std::clamp(Steering, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetHandbrakeInput(bool bNewHandbrake)
{
	bRawHandbrake = bNewHandbrake;
}

float UWheeledVehicleMovementComponent::GetForwardSpeed() const
{
	return PVehicle ? PVehicle->computeForwardSpeed() : 0.0f;
}

void UWheeledVehicleMovementComponent::PreTick(float SubstepDeltaTime)
{
	if (!PVehicle || SubstepDeltaTime <= 0.0f)
	{
		return;
	}

	constexpr float ReverseSpeedThreshold = 0.5f;
	const float ForwardSpeed = PVehicle->computeForwardSpeed();
	const physx::PxU32 CurrentGear = PVehicle->mDriveDynData.getCurrentGear();

	float EffectiveThrottle = RawThrottle;
	float EffectiveBrake = RawBrake;

	if (CurrentGear == physx::PxVehicleGearsData::eREVERSE)
	{
		if (RawThrottle > 0.01f)
		{
			if (ForwardSpeed > -ReverseSpeedThreshold)
			{
				PVehicle->mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eFIRST);
			}
			else
			{
				// 후진 중 전진 입력은 감속(브레이크)으로 처리
				EffectiveThrottle = 0.0f;
				EffectiveBrake = RawThrottle;
			}
		}
		else if (RawBrake > 0.01f)
		{
			// 후진 기어에서 브레이크 입력은 후진 가속(스로틀)으로 전환
			EffectiveThrottle = RawBrake;
			EffectiveBrake = 0.0f;
		}
	}
	else
	{
		if (RawBrake > 0.01f && RawThrottle <= 0.01f && ForwardSpeed < ReverseSpeedThreshold)
		{
			PVehicle->mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eREVERSE);
			EffectiveThrottle = RawBrake;
			EffectiveBrake = 0.0f;
		}
	}

	physx::PxVehicleDriveNWRawInputData RawInputData;
	RawInputData.setAnalogAccel(EffectiveThrottle);
	RawInputData.setAnalogBrake(EffectiveBrake);
	RawInputData.setAnalogHandbrake(bRawHandbrake ? 1.0f : 0.0f);
	RawInputData.setAnalogSteer(RawSteering);

	static const physx::PxVehiclePadSmoothingData PadSmoothingData =
	{
		{
			6.0f,  // eANALOG_INPUT_ACCEL rise rate
			6.0f,  // eANALOG_INPUT_BRAKE rise rate
			12.0f, // eANALOG_INPUT_HANDBRAKE rise rate
			2.5f,  // eANALOG_INPUT_STEER_LEFT rise rate
			2.5f   // eANALOG_INPUT_STEER_RIGHT rise rate
		},
		{
			10.0f, // eANALOG_INPUT_ACCEL fall rate
			10.0f, // eANALOG_INPUT_BRAKE fall rate
			12.0f, // eANALOG_INPUT_HANDBRAKE fall rate
			5.0f,  // eANALOG_INPUT_STEER_LEFT fall rate
			5.0f   // eANALOG_INPUT_STEER_RIGHT fall rate
		}
	};

	static const physx::PxF32 SteerVsForwardSpeedData[2 * 8] =
	{
		0.0f,   1.0f,
		5.0f,   0.75f,
		30.0f,  0.125f,
		120.0f, 0.1f,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32
	};
	static const physx::PxFixedSizeLookupTable<8> SteerVsForwardSpeedTable(SteerVsForwardSpeedData, 4);

	bool bIsVehicleInAir = !WheelStates.IsEmpty();
	for (const FWheelRuntimeState& State : WheelStates)
	{
		if (!State.bIsInAir)
		{
			bIsVehicleInAir = false;
			break;
		}
	}

	physx::PxVehicleDriveNWSmoothAnalogRawInputsAndSetAnalogInputs(
		PadSmoothingData,
		SteerVsForwardSpeedTable,
		RawInputData,
		SubstepDeltaTime,
		bIsVehicleInAir,
		*PVehicle);
}

void UWheeledVehicleMovementComponent::UpdateWheelBones()
{
	if (!PVehicle)
	{
		return;
	}

	USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(UpdatedComponent);
	FPhysScene* PhysScene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	FPhysXVehicleManager* VehicleManager = PhysScene ? PhysScene->GetVehicleManager() : nullptr;
	if (!SkelComp || !VehicleManager)
	{
		return;
	}

	const physx::PxVehicleWheelQueryResult* Results = VehicleManager->GetWheelQueryResults(PVehicle);
	if (!Results || !Results->wheelQueryResults)
	{
		return;
	}

	const int32 Count = std::min(WheelStates.Num(), static_cast<int32>(Results->nbWheelQueryResults));
	bool bAnyBoneUpdated = false;

	for (int32 i = 0; i < Count; ++i)
	{
		const physx::PxWheelQueryResult& WheelResult = Results->wheelQueryResults[i];
		FWheelRuntimeState& State = WheelStates[i];

		State.SuspOffset = WheelResult.suspJounce;
		State.SteerAngle = WheelResult.steerAngle;
		State.RotationAngle = PVehicle->mWheelsDynData.getWheelRotationAngle(static_cast<physx::PxU32>(i));
		State.bIsInAir = WheelResult.isInAir;

		if (State.BoneIndex != -1)
		{
			FTransform NewTM = State.InitialBoneLocalTransform;
			NewTM.Location.Z += State.SuspOffset;

			const FQuat SteerQuat = FQuat::MakeFromAxisAngle(UpVector, State.SteerAngle);
			const FQuat SpinQuat = FQuat::MakeFromAxisAngle(RightVector, State.RotationAngle);
			NewTM.Rotation = State.InitialBoneLocalTransform.Rotation * SteerQuat * SpinQuat;

			SkelComp->SetBoneLocalTransform(State.BoneIndex, NewTM);
			bAnyBoneUpdated = true;
		}
	}

	if (bAnyBoneUpdated)
	{
		SkelComp->RefreshBoneTransformsNow();
	}
}
