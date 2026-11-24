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
}

FVector AWheeledVehicle::GetVelocity() const
{
	if (!VehicleMovement || !GetRootComponent())
	{
		return FVector::ZeroVector;
	}

	return GetActorRotation().Quaternion().GetForwardVector() * VehicleMovement->GetForwardSpeed();
}
