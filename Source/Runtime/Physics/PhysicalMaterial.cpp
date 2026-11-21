#include "EnginePCH.h"
#include "Physics/PhysicalMaterial.h"

const FPhysicsMaterialHandle& UPhysicalMaterial::GetPhysicsMaterial()
{
	if (!MaterialHandle.IsValid())
	{
		MaterialHandle = FPhysicsInterface::CreateMaterial(this);
		UpdateMaterial();
	}
	return MaterialHandle;
}

void UPhysicalMaterial::UpdateMaterial()
{
	FPhysicsInterface::UpdateMaterial(MaterialHandle, *this);
}

void UPhysicalMaterial::PostEditChangeProperty(const FProperty& Property)
{
	UpdateMaterial();
	Super::PostEditChangeProperty(Property);
}

UPhysicalMaterial::~UPhysicalMaterial()
{
	FPhysicsInterface::ReleaseMaterial(MaterialHandle);
}
