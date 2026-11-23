#include "EnginePCH.h"
#include "GameFramework/WheeledVehicle.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Physics/BodySetup.h"
#include "UObject/UObjectGlobals.h"

AWheeledVehicle::AWheeledVehicle()
{
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
}

void AWheeledVehicle::BeginPlay()
{
	if (Mesh && VehicleMovement)
	{
		Mesh->SetUseArticulatedPhysics(false);
		VehicleMovement->SetUpdatedComponent(Mesh);

		// PhysicsAsset이 없는 테스트용 메쉬도 즉시 주행 테스트가 가능하도록 박스 콜리전 폴백 제공
		if (!Mesh->GetBodySetup())
		{
			FallbackBodySetup = NewObject<UBodySetup>(this);
			FKBoxElem BoxElem;
			BoxElem.X = VehicleMovement->ChassisDimensions.X;
			BoxElem.Y = VehicleMovement->ChassisDimensions.Y;
			BoxElem.Z = VehicleMovement->ChassisDimensions.Z;
			FallbackBodySetup->AggGeom.BoxElems.Add(BoxElem);
			Mesh->SetSingleBodySetupOverride(FallbackBodySetup);
		}
	}

	Super::BeginPlay();
}

FVector AWheeledVehicle::GetVelocity() const
{
	if (!VehicleMovement || !GetRootComponent())
	{
		return FVector::ZeroVector;
	}

	return GetActorRotation().Quaternion().GetForwardVector() * VehicleMovement->GetForwardSpeed();
}
