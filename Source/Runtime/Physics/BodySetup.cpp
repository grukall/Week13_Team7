#include "EnginePCH.h"
#include "Physics/BodySetup.h"
#include "Physics/BodyInstance.h"
#include "Engine/Engine.h"
#include <PxPhysicsAPI.h>
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/MathSerialization.h"
#include <cstring>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();
	const bool bLoading = Ar.IsLoading();

	int32 NumBoxes = bLoading ? 0 : static_cast<int32>(AggGeom.BoxElems.Num());
	auto Boxes = Record.EnterArray("Boxes", NumBoxes);
	if (bLoading) AggGeom.BoxElems.SetNum(NumBoxes);
	for (int32 i = 0; i < NumBoxes; ++i)
	{
		FKBoxElem& E = AggGeom.BoxElems[i];
		auto R = Boxes.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center);
		SerializeQuat(R.EnterRecord("Rotation"), E.Rotation);
		R << SA_VALUE("X", E.X) << SA_VALUE("Y", E.Y) << SA_VALUE("Z", E.Z);
	}

	int32 NumSpheres = bLoading ? 0 : static_cast<int32>(AggGeom.SphereElems.Num());
	auto Spheres = Record.EnterArray("Spheres", NumSpheres);
	if (bLoading) AggGeom.SphereElems.SetNum(NumSpheres);
	for (int32 i = 0; i < NumSpheres; ++i)
	{
		FKSphereElem& E = AggGeom.SphereElems[i];
		auto R = Spheres.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center) << SA_VALUE("Radius", E.Radius);
	}

	int32 NumCapsules = bLoading ? 0 : static_cast<int32>(AggGeom.SphylElems.Num());
	auto Capsules = Record.EnterArray("Capsules", NumCapsules);
	if (bLoading) AggGeom.SphylElems.SetNum(NumCapsules);
	for (int32 i = 0; i < NumCapsules; ++i)
	{
		FKSphylElem& E = AggGeom.SphylElems[i];
		auto R = Capsules.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center);
		SerializeQuat(R.EnterRecord("Rotation"), E.Rotation);
		R << SA_VALUE("Radius", E.Radius) << SA_VALUE("Length", E.Length);
	}

	// Convex는 꼭짓점만 저장한다. PhysX 메시는 로드 후 CreatePhysicsMeshes가 다시 쿠킹한다
	int32 NumConvex = bLoading ? 0 : static_cast<int32>(AggGeom.ConvexElems.Num());
	auto Convexes = Record.EnterArray("Convexes", NumConvex);
	if (bLoading) AggGeom.ConvexElems.SetNum(NumConvex);
	for (int32 i = 0; i < NumConvex; ++i)
	{
		TArray<FVector>& Points = AggGeom.ConvexElems[i].VertexData;
		int32 NumPoints = bLoading ? 0 : static_cast<int32>(Points.Num());
		auto PointArray = Convexes.EnterElement().EnterArray(NumPoints);
		if (bLoading) Points.SetNum(NumPoints);
		for (int32 p = 0; p < NumPoints; ++p)
			PointArray.EnterElement() << Points[p];
	}

	if (bLoading)
		InvalidatePhysicsData();
}

namespace
{
	using namespace physx;

	bool CookPoints(const TArray<PxVec3>& Points, PxCooking& Cooking, TArray<uint8>& Bytes)
	{
		PxConvexMeshDesc Desc;
		Desc.points.count = Points.Num();
		Desc.points.stride = sizeof(PxVec3);
		Desc.points.data = Points.GetData();
		Desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
		PxDefaultMemoryOutputStream Output;
		if (!Cooking.cookConvexMesh(Desc, Output))
			return false;
		Bytes.SetNum(Output.getSize());
		std::memcpy(Bytes.GetData(), Output.getData(), Output.getSize());
		return true;
	}

	TSharedPtr<PxConvexMesh> LoadMesh(TArray<uint8>& Bytes, PxPhysics& Physics)
	{
		PxDefaultMemoryInputData Input(Bytes.GetData(), Bytes.Num());
		if (auto* Mesh = Physics.createConvexMesh(Input))
			return TSharedPtr<PxConvexMesh>(Mesh, [](PxConvexMesh* Owned) { Owned->release(); });
		return {};
	}
}

namespace
{
	// UE 4.27 SetupNonUniformHelper substitutes a nonzero shape scale for a collapsed transform.
	FVector ShapeScaleAbs(const FVector& Scale3D)
	{
		const FVector Size = Scale3D.GetAbs();
		if (std::max({ Size.X, Size.Y, Size.Z }) <= KINDA_SMALL_NUMBER)
			return FVector(0.1f, 0.1f, 0.1f);
		return Size;
	}
}

FKSphereElem FKSphereElem::GetFinalScaled(const FVector& Scale3D) const
{
	const FVector Size = ShapeScaleAbs(Scale3D);
	const float MinScale = std::min({ Size.X, Size.Y, Size.Z });
	return { Radius * (MinScale <= SMALL_NUMBER ? 0.1f : MinScale), Center * Scale3D };
}

FKSphylElem FKSphylElem::GetFinalScaled(const FVector& Scale3D) const
{
	const FVector Size = ShapeScaleAbs(Scale3D);
	return { GetScaledRadius(Size), GetScaledCylinderLength(Size), Center * Scale3D, Rotation };
}

float FKSphylElem::GetScaledHalfLength(const FVector& Scale3D) const
{
	return std::max((Length * 0.5f + Radius) * std::abs(Scale3D.Z), 0.1f * CmToWorld);
}

float FKSphylElem::GetScaledRadius(const FVector& Scale3D) const
{
	const FVector Size = Scale3D.GetAbs();
	return std::clamp(Radius * std::max(Size.X, Size.Y), 0.1f * CmToWorld, GetScaledHalfLength(Scale3D));
}

float FKSphylElem::GetScaledCylinderLength(const FVector& Scale3D) const
{
	return std::max((GetScaledHalfLength(Scale3D) - GetScaledRadius(Scale3D)) * 2.0f, 0.1f * CmToWorld);
}

bool UBodySetup::CookConvex(const FKConvexElem& Hull, std::array<TArray<uint8>, 2>& Streams)
{
	if (Hull.VertexData.Num() < 4 || !GEngine || !GEngine->GetPhysXRuntime().Cooking)
		return false;
	TArray<PxVec3> Points;
	for (const FVector& V : Hull.VertexData)
	{
		const PxVec3 Point(V.X, V.Y, V.Z);
		if (!Point.isFinite())
			return false;
		Points.Add(Point);
	}
	auto& Cooking = *GEngine->GetPhysXRuntime().Cooking;
	TArray<uint8> Normal, Mirrored;
	if (!CookPoints(Points, Cooking, Normal))
		return false;
	for (auto& Point : Points)
		Point.x = -Point.x;
	if (!CookPoints(Points, Cooking, Mirrored))
		return false;
	Streams[0] = std::move(Normal);
	Streams[1] = std::move(Mirrored);
	return true;
}

bool UBodySetup::CreatePhysicsMeshes()
{
	for (int32 Index = 0; Index < AggGeom.ConvexElems.Num(); ++Index)
	{
		auto& Elem = AggGeom.ConvexElems[Index];
		if (Elem.ConvexMesh && Elem.ConvexMeshNegX)
			continue;
		// Only transient or explicitly invalidated geometry needs a runtime cook.
		std::array<TArray<uint8>, 2> RuntimeStreams;
		auto& Streams = CookedFormatData.IsValidIndex(Index) ? CookedFormatData[Index] : RuntimeStreams;
		if (Streams[0].IsEmpty() && !CookConvex(Elem, Streams))
			return false;
		auto& Physics = *GEngine->GetPhysXRuntime().Physics;
		auto Normal = LoadMesh(Streams[0], Physics);
		auto Mirrored = LoadMesh(Streams[1], Physics);
		if (!Normal || !Mirrored)
			return false;
		Elem.ConvexMesh = std::move(Normal);
		Elem.ConvexMeshNegX = std::move(Mirrored);
	}
	return true;
}

bool UBodySetup::AddShapesToRigidActor_AssumesLocked(FBodyInstance* OwningInstance, const FVector& Scale3D)
{
	if (AggGeom.IsEmpty() || !CreatePhysicsMeshes())
		return false;
	FGeometryAddParams Params;
	Params.Geometry = &AggGeom;
	Params.SimpleMaterial = OwningInstance->GetSimplePhysicalMaterial();
	Params.Scale = Scale3D;
	return FPhysicsInterface::AddGeometry(OwningInstance->ActorHandle, Params);
}

void UBodySetup::InvalidatePhysicsData()
{
	for (FKConvexElem& Elem : AggGeom.ConvexElems)
	{
		Elem.ConvexMesh.reset();
		Elem.ConvexMeshNegX.reset();
	}
	CookedFormatData.Empty();
}

namespace
{
	// Check serialized data before passing opaque mesh bytes to the SDK.
	template<typename T>
	bool WriteArray(std::ostream& Out, const TArray<T>& Values)
	{
		const uint32 Count = Values.Num();
		const auto Size = size_t(Count) * sizeof(T);
		std::array<uint8, 32> Digest;
		if (Size > ULONG_MAX || BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)Values.GetData(), ULONG(Size), Digest.data(), ULONG(Digest.size())) < 0)
			return false;
		Out.write((const char*)&Count, sizeof(Count));
		Out.write((const char*)Digest.data(), Digest.size());
		return bool(Out.write((const char*)Values.GetData(), Size));
	}

	template<typename T>
	bool ReadArray(std::istream& In, TArray<T>& Values, uint32 Limit)
	{
		uint32 Count;
		std::array<uint8, 32> Digest, Actual;
		if (!In.read((char*)&Count, sizeof(Count)) || !Count || Count > Limit || !In.read((char*)Digest.data(), Digest.size()))
			return false;
		Values.SetNum(Count);
		const auto Size = ULONG(Count * sizeof(T));
		return In.read((char*)Values.GetData(), Size) &&
			BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)Values.GetData(), Size, Actual.data(), ULONG(Actual.size())) >= 0 && Digest == Actual;
	}
}

bool WriteCookedConvex(std::ostream& Out, const TArray<FKConvexElem>& Hulls, const TArray<std::array<TArray<uint8>, 2>>& CookedFormatData)
{
	const uint32 Version = PX_PHYSICS_VERSION, Count = Hulls.Num();
	Out.write((const char*)&Version, sizeof(Version));
	Out.write((const char*)&Count, sizeof(Count));
	if (Hulls.Num() != CookedFormatData.Num())
		return false;
	for (int32 I = 0; I < Hulls.Num(); ++I)
	{
		const auto& Hull = Hulls[I];
		const auto& Streams = CookedFormatData[I];
		if (Hull.VertexData.Num() < 4 || Streams[0].IsEmpty() || Streams[1].IsEmpty() ||
			!WriteArray(Out, Hull.VertexData) || !WriteArray(Out, Streams[0]) || !WriteArray(Out, Streams[1]))
			return false;
	}
	return bool(Out);
}

bool ReadCookedConvex(std::istream& In, TArray<FKConvexElem>& Hulls, TArray<std::array<TArray<uint8>, 2>>& CookedFormatData)
{
	uint32 Version, Count;
	if (!In.read((char*)&Version, sizeof(Version)) || !In.read((char*)&Count, sizeof(Count)) || Version != PX_PHYSICS_VERSION || Count > 65536)
		return false;
	for (uint32 I = 0; I < Count; ++I)
	{
		FKConvexElem Hull;
		std::array<TArray<uint8>, 2> Streams;
		if (!ReadArray(In, Hull.VertexData, 1000000) || Hull.VertexData.Num() < 4 ||
			!ReadArray(In, Streams[0], 1024 * 1024) || !ReadArray(In, Streams[1], 1024 * 1024))
			return false;
		Hulls.Add(std::move(Hull));
		CookedFormatData.Add(std::move(Streams));
	}
	return In.peek() == std::char_traits<char>::eof() && !In.bad();
}
