#pragma once

#include "Physics/PhysicalMaterial.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Physics/CookedConvex.h"
#include "BodySetup.generated.h"

// Simple collision geometry in body-local space. Box dimensions are full lengths;
// sphyl Length excludes the two hemispheres. Convex points are in body-local space.
struct FKBoxElem
{
	float X = 0, Y = 0, Z = 0;
	FVector Center = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity();
};

struct FKSphereElem
{
	float Radius = 0;
	FVector Center = FVector::ZeroVector;

	FKSphereElem GetFinalScaled(const FVector& Scale3D) const;
};

struct FKSphylElem
{
	float Radius = 0;
	float Length = 0;
	FVector Center = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity();

	float GetScaledRadius(const FVector& Scale3D) const;
	float GetScaledCylinderLength(const FVector& Scale3D) const;
	float GetScaledHalfLength(const FVector& Scale3D) const;
	FKSphylElem GetFinalScaled(const FVector& Scale3D) const;
};

struct FBodyInstance;
struct FKAggregateGeom
{
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphereElem> SphereElems;
	TArray<FKSphylElem> SphylElems;
	TArray<FKConvexElem> ConvexElems;

	bool IsEmpty() const { return BoxElems.IsEmpty() && SphereElems.IsEmpty() && SphylElems.IsEmpty() && ConvexElems.IsEmpty(); }
};

UCLASS()
class UBodySetup : public UObject
{
	GENERATED_BODY()

public:
	// "Properties"(PhysMaterial 경로 등)는 리플렉션, AggGeom 도형 배열은 직접 쓴다. 쿠킹 결과는 저장하지 않는다
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	UPROPERTY(EditAnywhere)
	UPhysicalMaterial* PhysMaterial = nullptr;
	FKAggregateGeom AggGeom;
	TArray<std::array<TArray<uint8>, 2>> CookedFormatData;
	static bool CookConvex(const FKConvexElem& Hull, std::array<TArray<uint8>, 2>& Streams);
	bool CreatePhysicsMeshes();
	bool AddShapesToRigidActor_AssumesLocked(FBodyInstance* OwningInstance, const FVector& Scale3D);
	// Call after editing authored convex geometry, before recreating bodies.
	void InvalidatePhysicsData();
};
