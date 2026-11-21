#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "Physics/BodySetup.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::SetCollisionEnabled(ECollisionEnabled Enabled)
{
	BodyInstance.CollisionEnabled = Enabled;
	BodyInstance.UpdatePhysicsFilterData();
}

void UPrimitiveComponent::SetCollisionObjectType(ECollisionChannel Channel)
{
	BodyInstance.ObjectType = Channel;
	BodyInstance.UpdatePhysicsFilterData();
}

void UPrimitiveComponent::SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response)
{
	BodyInstance.CollisionResponses.SetResponse(Channel, Response);
	BodyInstance.UpdatePhysicsFilterData();
}

void UPrimitiveComponent::SetNotifyRigidBodyCollision(bool bNotify)
{
	BodyInstance.bNotifyRigidBodyCollision = bNotify;
	BodyInstance.UpdatePhysicsFilterData();
}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

bool UPrimitiveComponent::ShouldCreatePhysicsState()
{
	const UBodySetup* Setup = GetBodySetup();
	return Setup && !Setup->AggGeom.IsEmpty();
}

void UPrimitiveComponent::OnCreatePhysicsState()
{
	const FTransform BodyTransform(GetWorldRotation(), GetWorldLocation(), GetWorldScale3D());
	BodyInstance.InitBody(GetBodySetup(), BodyTransform, this, GetWorld()->GetPhysicsScene());
	if (BodyInstance.IsValidBodyInstance())
		Super::OnCreatePhysicsState();
	else
		HTR_LOG(Warning, "Failed to create physics state for {}", GetName());
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	BodyInstance.TermBody();
	Super::OnDestroyPhysicsState();
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnUpdateTransform()
{
	if (BodyInstance.IsValidBodyInstance())
		BodyInstance.SetBodyTransform(FTransform(GetWorldRotation(), GetWorldLocation(), GetWorldScale3D()));
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	if (!bSweep || !GetWorld() || Delta.SizeSquared() <= SMALL_NUMBER)
		return Super::MoveComponent(Delta, NewRotation, false, Hit);
	const FVector Start = GetWorldLocation();
	TArray<FHitResult> Hits;
	GetWorld()->ComponentSweepMulti(Hits, this, Start, Start + Delta, GetWorldRotation().Quaternion());
	FVector Destination = Start + Delta;
	const FHitResult* BlockingHit = nullptr;
	for (const auto& Candidate : Hits)
	{
		if (!Candidate.bBlockingHit || (Candidate.bStartPenetrating && Delta.Dot(Candidate.Normal) >= 0.f))
			continue;
		BlockingHit = &Candidate;
		Destination = Start + Delta * Candidate.Time;
		break;
	}
	if (Hit)
		*Hit = BlockingHit ? *BlockingHit : FHitResult(1.0f);
	const FRotator OldRotation = GetWorldRotation();
	const bool Moved = (Destination - Start).SizeSquared() > 0.f || OldRotation.Pitch != NewRotation.Pitch ||
		OldRotation.Yaw != NewRotation.Yaw || OldRotation.Roll != NewRotation.Roll;
	SetWorldLocationAndRotation(Destination, NewRotation, false);
	for (const auto& Candidate : Hits)
	{
		if (&Candidate == BlockingHit)
			break;
		auto* Other = Candidate.HitComponent;
		if (!Candidate.bBlockingHit && bGenerateOverlapEvents && Other->bGenerateOverlapEvents && !IsOverlappingComponent(Other))
		{
			BeginComponentOverlap(Other);
			Other->BeginComponentOverlap(this);
		}
	}
	if (BlockingHit)
		DispatchHit(BlockingHit->HitComponent);
	return Moved;
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	BodyInstance.UpdatePhysicsFilterData();
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" ||
		Property.Name == "bReceivesDecals" ||
		Property.Name == "TranslucencySortPriority")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}
