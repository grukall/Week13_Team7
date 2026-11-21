#pragma once

#include "Math/Vector.h"

namespace ECollisionShape
{
	enum Type { Line, Box, Sphere, Capsule };
}

struct FCollisionShape
{
	ECollisionShape::Type ShapeType = ECollisionShape::Line;
	FVector HalfExtent = FVector::ZeroVector;

	static FCollisionShape MakeBox(const FVector& HalfExtent) { return {ECollisionShape::Box, HalfExtent}; }
	static FCollisionShape MakeSphere(float Radius) { return {ECollisionShape::Sphere, {Radius, Radius, Radius}}; }
	static FCollisionShape MakeCapsule(float Radius, float HalfHeight) { return {ECollisionShape::Capsule, {Radius, Radius, HalfHeight}}; }
};
