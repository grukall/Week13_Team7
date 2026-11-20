#include "EnginePCH.h"
#include "Engine/World.h"
#include "Collision/CollisionQueryParams.h"
#include "Physics/PhysScene.h"
#include "UObject/UObjectIterator.h"
#include "Physics/PhysicsFiltering.h"
#include <PxPhysicsAPI.h>

namespace
{
	using namespace physx;

	PxVec3 ToPx(const FVector& V) { return {V.X, V.Y, V.Z}; }
	FVector FromPx(const PxVec3& V) { return {V.x, V.y, V.z}; }

	UPrimitiveComponent* ComponentOf(const PxRigidActor* Actor)
	{
		auto* Body = static_cast<FBodyInstance*>(Actor->userData);
		return Body ? Body->GetOwnerComponent() : nullptr;
	}

	class FCollisionQueryFilterCallback final : public PxQueryFilterCallback
	{
	public:
		ECollisionChannel Channel;
		const AActor* IgnoreActor;
		const UPrimitiveComponent* Source;
		bool Touches;
		const FCollisionObjectQueryParams* ObjectParams = nullptr;

		FCollisionQueryFilterCallback(ECollisionChannel Channel, const AActor* Ignore, const UPrimitiveComponent* Source, bool Touches)
			: Channel(Channel), IgnoreActor(Ignore), Source(Source), Touches(Touches) {}

		PxQueryHitType::Enum CalcQueryHitType(const PxShape& Shape) const
		{
			const auto Data = Shape.getQueryFilterData();
			if (ObjectParams)
			{
				const uint32 ShapeType = 1u << static_cast<uint32>(GetCollisionChannel(Data.word3));
				return ObjectParams->ObjectTypesToQuery & ShapeType ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
			}
			const uint32 Bit = 1u << static_cast<uint32>(Channel);
			auto Response = Data.word1 & Bit ? ECollisionResponse::ECR_Block :
				Data.word2 & Bit ? ECollisionResponse::ECR_Overlap : ECollisionResponse::ECR_Ignore;
			if (Source)
				Response = std::min(Response, Source->BodyInstance.CollisionResponses.GetResponse(GetCollisionChannel(Data.word3)));
			if (Response == ECollisionResponse::ECR_Block)
				return PxQueryHitType::eBLOCK;
			return Touches && Response == ECollisionResponse::ECR_Overlap ? PxQueryHitType::eTOUCH : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			auto* Component = ComponentOf(Actor);
			if (!Component || Component == Source || (IgnoreActor && Component->GetOwner() == IgnoreActor))
				return PxQueryHitType::eNONE;
			return CalcQueryHitType(*Shape);
		}
		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override { return PxQueryHitType::eNONE; }
	};

	template<class HitType>
	class TQueryHits final : public PxHitCallback<HitType>
	{
		HitType Buffer[32];
	public:
		TArray<HitType> Hits;
		TQueryHits() : PxHitCallback<HitType>(Buffer, 32) {}
		PxAgain processTouches(const HitType* Chunk, PxU32 Count) override
		{
			for (PxU32 I = 0; I < Count; ++I)
				Hits.Add(Chunk[I]);
			return true;
		}
	};

	FHitResult ConvertQueryImpactHit(const PxLocationHit& Hit, const FVector& Start, const FVector& End, float Length, bool Initial, bool Blocking)
	{
		FHitResult Result;
		Result.Init(Start, End);
		Result.bBlockingHit = Blocking;
		Result.bStartPenetrating = Initial;
		Result.PenetrationDepth = Initial ? std::max(0.f, -Hit.distance) : 0.f;
		Result.Distance = std::max(0.f, Hit.distance);
		Result.Time = Result.Distance / Length;
		Result.Location = Start + (End - Start) * Result.Time;
		Result.ImpactPoint = Initial ? Start : FromPx(Hit.position);
		Result.Normal = Result.ImpactNormal = FromPx(Hit.normal);
		Result.HitComponent = ComponentOf(Hit.actor);
		Result.FaceIndex = static_cast<int32>(Hit.faceIndex);
		return Result;
	}

	bool CanQuery(const UPrimitiveComponent* Component)
	{
		if (!Component || !Component->BodyInstance.IsValidBodyInstance())
			return false;
		const auto Enabled = Component->BodyInstance.CollisionEnabled;
		return (!Component->GetOwner() || Component->GetOwner()->GetActorEnableCollision()) &&
			(Enabled == ECollisionEnabled::QueryOnly || Enabled == ECollisionEnabled::QueryAndPhysics);
	}

	const PxQueryFlags QueryFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
}

bool UWorld::LineTraceSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel Channel, const AActor* IgnoreActor) const
{
	return SweepSingleByChannel(OutHit, Start, End, FQuat::Identity(), Channel, FCollisionShape{}, IgnoreActor);
}

static bool SweepSingle(physx::PxScene* Scene, FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rotation, const FCollisionShape& CollisionShape, FCollisionQueryFilterCallback Filter, bool bReturnPhysicalMaterial)
{
	using namespace physx;
	OutHit = FHitResult(1.f);
	OutHit.Init(Start, End);
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!Scene || !(Length > SMALL_NUMBER) || !std::isfinite(Length))
		return false;
	PxSceneReadLock Lock(*Scene);
	const PxQueryFilterData Data(QueryFlags);
	const PxVec3 Direction = ToPx(Delta / Length);
	const PxShape* HitShape = nullptr;
	if (CollisionShape.ShapeType == ECollisionShape::Line || CollisionShape.HalfExtent.SizeSquared() <= SMALL_NUMBER)
	{
		PxRaycastBuffer Buffer;
		if (!Scene->raycast(ToPx(Start), Direction, Length, Buffer, PxHitFlag::eDEFAULT, Data, &Filter))
			return false;
		HitShape = Buffer.block.shape;
		OutHit = ConvertQueryImpactHit(Buffer.block, Start, End, Length, false, true);
	}
	else
	{
		PxGeometryHolder Geometry;
		const auto& Extent = CollisionShape.HalfExtent;
		PxTransform Pose(ToPx(Start), PxQuat(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
		switch (CollisionShape.ShapeType)
		{
		case ECollisionShape::Box:
			if (!PxBoxGeometry(ToPx(Extent)).isValid())
				return false;
			Geometry.storeAny(PxBoxGeometry(ToPx(Extent)));
			break;
		case ECollisionShape::Sphere:
			if (!PxSphereGeometry(Extent.X).isValid())
				return false;
			Geometry.storeAny(PxSphereGeometry(Extent.X));
			break;
		case ECollisionShape::Capsule:
		{
			PxCapsuleGeometry Capsule(Extent.X, std::max(Extent.Z - Extent.X, 0.f));
			if (!Capsule.isValid())
				return false;
			Geometry.storeAny(Capsule);
			Pose.q = Pose.q * PxQuat(-PxHalfPi, PxVec3(0, 1, 0));
			break;
		}
		default:
			return false;
		}
		if (!Pose.isValid())
			return false;
		PxSweepBuffer Buffer;
		if (!Scene->sweep(Geometry.any(), Pose, Direction, Length, Buffer, PxHitFlag::eDEFAULT | PxHitFlag::eMTD, Data, &Filter))
			return false;
		HitShape = Buffer.block.shape;
		OutHit = ConvertQueryImpactHit(Buffer.block, Start, End, Length, Buffer.block.hadInitialOverlap(), true);
	}
	if (bReturnPhysicalMaterial)
	{
		// The supported simple geometries bind one material per shape.
		PxMaterial* Material = nullptr;
		if (HitShape->getMaterials(&Material, 1))
			OutHit.PhysMaterial = static_cast<UPhysicalMaterial*>(Material->userData);
	}
	return true;
}

bool UWorld::SweepSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rotation, ECollisionChannel Channel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const
{
	return SweepSingle(PhysicsScene ? PhysicsScene->Scene : nullptr, OutHit, Start, End, Rotation, CollisionShape, {Channel, IgnoreActor, nullptr, false}, false);
}

bool UWorld::LineTraceSingleByObjectType(FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionObjectQueryParams& ObjectParams, const AActor* IgnoreActor, bool bReturnPhysicalMaterial) const
{
	return SweepSingleByObjectType(OutHit, Start, End, FQuat::Identity(), ObjectParams, FCollisionShape{}, IgnoreActor, bReturnPhysicalMaterial);
}

bool UWorld::SweepSingleByObjectType(FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rotation, const FCollisionObjectQueryParams& ObjectParams, const FCollisionShape& CollisionShape, const AActor* IgnoreActor, bool bReturnPhysicalMaterial) const
{
	FCollisionQueryFilterCallback Filter({}, IgnoreActor, nullptr, false);
	Filter.ObjectParams = &ObjectParams;
	return SweepSingle(PhysicsScene ? PhysicsScene->Scene : nullptr, OutHit, Start, End, Rotation, CollisionShape, Filter, bReturnPhysicalMaterial);
}

void FPhysicsInterface::GeomSweepMulti(const UWorld& World, const physx::PxGeometry& Geometry, const physx::PxTransform& ShapePose, const UPrimitiveComponent& Source, const FVector& Start, const FVector& End, float Length, TArray<FHitResult>& Hits)
{
	using namespace physx;
	FCollisionQueryFilterCallback Filter(Source.BodyInstance.ObjectType, Source.GetOwner(), &Source, true);
	const PxQueryFilterData Data(QueryFlags | PxQueryFlag::eNO_BLOCK);
	TQueryHits<PxSweepHit> Buffer;
	auto& Scene = *World.GetPhysicsScene()->Scene;
	Scene.sweep(Geometry, ShapePose, ToPx((End - Start) / Length), Length, Buffer, PxHitFlag::eDEFAULT | PxHitFlag::eMTD, Data, &Filter);
	for (const auto& Hit : Buffer.Hits)
	{
		const bool Block = Filter.CalcQueryHitType(*Hit.shape) == PxQueryHitType::eBLOCK;
		Hits.Add(ConvertQueryImpactHit(Hit, Start, End, Length, Hit.hadInitialOverlap(), Block));
	}
}

bool UWorld::ComponentSweepMulti(TArray<FHitResult>& Hits, const UPrimitiveComponent* Component, const FVector& Start, const FVector& End, const FQuat& Rotation) const
{
	using namespace physx;
	Hits.Reset();
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!PhysicsScene || !CanQuery(Component) || Component->GetWorld() != this || !(Length > SMALL_NUMBER) || !std::isfinite(Length))
		return false;
	auto& Scene = *PhysicsScene->Scene;
	PxSceneReadLock Lock(Scene);
	auto* Actor = Component->BodyInstance.ActorHandle.SyncActor;
	const PxTransform Pose(ToPx(Start), PxQuat(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
	for (PxU32 I = 0; I < Actor->getNbShapes(); ++I)
	{
		PxShape* Shape;
		Actor->getShapes(&Shape, 1, I);
		FPhysicsInterface::GeomSweepMulti(*this, Shape->getGeometry().any(), Pose * Shape->getLocalPose(), *Component, Start, End, Length, Hits);
	}
	std::stable_sort(Hits.begin(), Hits.end(), [](const FHitResult& A, const FHitResult& B)
	{
		if (A.Distance == B.Distance)
			return A.bBlockingHit < B.bBlockingHit;
		return A.Distance < B.Distance;
	});
	bool InitialBlock = false;
	for (int32 Index = 0; Index < Hits.Num(); ++Index)
	{
		InitialBlock |= Hits[Index].bBlockingHit;
		if (Hits[Index].bBlockingHit && !Hits[Index].bStartPenetrating)
		{
			Hits.SetNum(Index + 1);
			return true;
		}
	}
	return InitialBlock;
}

void UWorld::ComponentOverlapMulti(TArray<UPrimitiveComponent*>& Overlaps, const UPrimitiveComponent* Component) const
{
	using namespace physx;
	Overlaps.Reset();
	if (!PhysicsScene || !CanQuery(Component) || Component->GetWorld() != this)
		return;
	auto& Scene = *PhysicsScene->Scene;
	PxSceneReadLock Lock(Scene);
	auto* Actor = Component->BodyInstance.ActorHandle.SyncActor;
	FCollisionQueryFilterCallback Filter(Component->BodyInstance.ObjectType, Component->GetOwner(), Component, true);
	const PxQueryFilterData Data(QueryFlags | PxQueryFlag::eNO_BLOCK);
	for (PxU32 I = 0; I < Actor->getNbShapes(); ++I)
	{
		PxShape* Shape;
		Actor->getShapes(&Shape, 1, I);
		TQueryHits<PxOverlapHit> Buffer;
		PxTransform Pose = Actor->getGlobalPose();
		if (auto* Dynamic = Actor->is<PxRigidDynamic>(); Dynamic && Dynamic->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
		{
			PxTransform Target;
			if (Dynamic->getKinematicTarget(Target))
				Pose = Target;
		}
		Scene.overlap(Shape->getGeometry().any(), Pose * Shape->getLocalPose(), Buffer, Data, &Filter);
		for (const auto& Hit : Buffer.Hits)
			if (Filter.CalcQueryHitType(*Hit.shape) == PxQueryHitType::eTOUCH)
				Overlaps.AddUnique(ComponentOf(Hit.actor));
	}
}

void UWorld::UpdateOverlaps()
{
	TArray<UPrimitiveComponent*> Overlaps;
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		auto* Component = *It;
		if (!Component->IsRegistered() || Component->GetWorld() != this)
			continue;
		Overlaps.Reset();
		if (Component->bGenerateOverlapEvents)
			ComponentOverlapMulti(Overlaps, Component);
		for (auto* Other : Overlaps)
			if (Other->bGenerateOverlapEvents && !Component->IsOverlappingComponent(Other))
			{
				Component->BeginComponentOverlap(Other);
				Other->BeginComponentOverlap(Component);
			}
		const auto Previous = Component->GetOverlapInfos();
		for (const auto& Info : Previous)
			if (!Info.OtherComp->bGenerateOverlapEvents || Overlaps.Find(Info.OtherComp) == -1)
			{
				Component->EndComponentOverlap(Info.OtherComp);
				Info.OtherComp->EndComponentOverlap(Component);
			}
	}
}
