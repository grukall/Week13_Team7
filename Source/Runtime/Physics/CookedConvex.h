#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"
#include <iosfwd>

#include <array>

namespace physx { class PxConvexMesh; }

struct FKConvexElem
{
	TArray<FVector> VertexData;
	TSharedPtr<physx::PxConvexMesh> ConvexMesh;
	TSharedPtr<physx::PxConvexMesh> ConvexMeshNegX;
};

// Win64 asset format: stream 0 is normal, stream 1 is X-mirrored.

bool WriteCookedConvex(std::ostream& Out, const TArray<FKConvexElem>& Hulls, const TArray<std::array<TArray<uint8>, 2>>& CookedFormatData);
bool ReadCookedConvex(std::istream& In, TArray<FKConvexElem>& Hulls, TArray<std::array<TArray<uint8>, 2>>& CookedFormatData);
