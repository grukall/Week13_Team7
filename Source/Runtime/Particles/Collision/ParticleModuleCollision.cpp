#include "EnginePCH.h"
#include "ParticleModuleCollision.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/Event/ParticleModuleEventGenerator.h"

namespace
{
	inline FVector Reflected(const FVector& V, const FVector& N)
	{
		return V - N * 2.0f * V.Dot(N);
	}
}

UParticleModuleCollision::UParticleModuleCollision()
{
	CollisionTypes.Add(ECollisionChannel::ECC_WorldStatic);
	bSpawnModule = true;
	bUpdateModule = true;
}

void UParticleModuleCollision::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	auto Field = Record.TryEnterField("CollisionTypes", true);
	if (!Field.IsSet())
		return;
	auto& Ar = Record.GetUnderlyingArchive();
	int32 Count = CollisionTypes.Num();
	auto Array = Field.GetValue().EnterArray(Count);
	const auto* Channels = TEnumInfo<ECollisionChannel>::Get();
	if (Ar.IsLoading())
		CollisionTypes.Reset();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FString Name = Ar.IsSaving() ? Channels->ToName(static_cast<int64>(CollisionTypes[Index])) : FString{};
		Array << Name;
		if (Ar.IsLoading())
			if (const auto* Entry = Channels->FindByName(Name))
			{
				const auto Channel = static_cast<ECollisionChannel>(Entry->Value);
				if (FCollisionObjectQueryParams::IsValidObjectQuery(Channel))
					CollisionTypes.AddUnique(Channel);
			}
	}
}

bool UParticleModuleCollision::PerformCollisionCheck(FParticleEmitterInstance* Owner, FHitResult& Hit, const AActor* SourceActor, const FVector& End, const FVector& Start, const FVector& Extent, const FCollisionObjectQueryParams& ObjectParams) const
{
	return Owner->Component->ParticleLineCheck(Hit, SourceActor, End, Start, Extent, ObjectParams);
}

void UParticleModuleCollision::Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase)
{
	assert(Offset != 0 && "Collision payload has no offset. Re-Init the emitter instance after changing modules.");

	SPAWN_INIT;
	PARTICLE_ELEMENT(FParticleCollisionPayload, CollisionPayload);

	CollisionPayload.UsedDampingFactor = DampingFactor.GetValue(Owner->EmitterTime);
	CollisionPayload.UsedCollisions = static_cast<int32>(std::lround(MaxCollisions.GetValue(Owner->EmitterTime)));
	CollisionPayload.Delay = DelayAmount.GetValue(Owner->EmitterTime);

	if (CollisionPayload.Delay > 0.0f)
	{
		Particle.Flags |= STATE_Particle_DelayCollisions;
	}
}

void UParticleModuleCollision::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
	assert(Offset != 0 && "Collision payload has no offset. Re-Init the emitter instance after changing modules.");

	// 해당 Emitter에 EventGenerator가 있으면 충돌을 알림
	const UParticleModuleEventGenerator* EventGenerator = nullptr;
	if (Owner->CurrentLODLevel)
	{
		for (UParticleModule* Module : Owner->CurrentLODLevel->Modules)
		{
			const UParticleModuleEventGenerator* Generator = Cast<UParticleModuleEventGenerator>(Module);
			if (Generator && Generator->bEnabled)
			{
				EventGenerator = Generator;
				break;
			}
		}
	}

	if (const UParticleSystemComponent* Component = Owner->Component)
	{
		if (Component->GetWorld())
		{
			FCollisionObjectQueryParams ObjectParams;
			for (const auto Channel : CollisionTypes)
				ObjectParams.AddObjectTypesToQuery(Channel);
			const AActor* IgnoreActor = bIgnoreSourceActor ? Component->GetOwner() : nullptr;
			const auto* LOD = Owner->CurrentLODLevel;
			const bool bLocal = LOD && LOD->RequiredModule && LOD->RequiredModule->bUseLocalSpace;
			const FMatrix ToWorld = bLocal ? Component->GetWorldMatrix() : FMatrix::Identity;
			const FMatrix FromWorld = bLocal ? ToWorld.Inverse() : FMatrix::Identity;
			const FVector Scale = Component->GetWorldScale3D().GetAbs();
			const auto* MeshType = LOD ? Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule) : nullptr;
			FVector MeshExtent = FVector::ZeroVector;
			if (MeshType && MeshType->StaticMesh)
			{
				const FBox& Bounds = MeshType->StaticMesh->GetMeshData().AABB;
				MeshExtent = (Bounds.Max - Bounds.Min) * 0.5f;
			}
			BEGIN_UPDATE_LOOP;

			if ((Particle.Flags & STATE_Particle_CollisionIgnoreCheck) != 0)
			{
				CONTINUE_UPDATE_LOOP;
			}

			PARTICLE_ELEMENT(FParticleCollisionPayload, CollisionPayload);

			if ((Particle.Flags & STATE_Particle_DelayCollisions) != 0)
			{
				if (CollisionPayload.Delay > Particle.RelativeTime)
				{
					CONTINUE_UPDATE_LOOP;
				}
				Particle.Flags &= ~STATE_Particle_DelayCollisions;
			}

			FHitResult Result;
			// PostSpawn already advanced newly born particles for the remainder of their first tick.
			const FVector Predicted = (Particle.Flags & STATE_Particle_JustSpawned) ? Particle.Location : Particle.Location + Particle.Velocity * DeltaTime;
			const FVector Start = bLocal ? ToWorld.TransformPosition(Particle.OldLocation) : Particle.OldLocation;
			const FVector Location = bLocal ? ToWorld.TransformPosition(Predicted) : Predicted;
			const FVector Travel = Location - Start;
			const FVector Direction = Travel.GetSafeNormal();
			const FVector Size = Particle.Size.GetAbs() * Scale;
			const FVector End = DirScalar > 0.f ? Location + Direction * Size / DirScalar : Location;
			const FVector Extent = MeshType && MeshType->bCollisionsConsiderPartilceSize ? MeshExtent * Size : MeshExtent;
			const bool bBlocked = PerformCollisionCheck(Owner, Result, IgnoreActor, End, Start, Extent, ObjectParams);

			if (bBlocked)
			{
				--CollisionPayload.UsedCollisions;

				if (EventGenerator)
				{
					EventGenerator->HandleParticleCollision(Owner, CollisionPayload, Result, Particle, Direction);
				}

				Particle.Flags |= STATE_Particle_CollisionHasOccurred;

				if (CollisionPayload.UsedCollisions > 0)
				{
					const FVector& Damping = CollisionPayload.UsedDampingFactor;
					const FVector BaseVelocity = bLocal ? ToWorld.TransformVector(Particle.BaseVelocity) : Particle.BaseVelocity;
					const FVector ReflectedVelocity = Reflected(BaseVelocity, Result.Normal) * Damping;
					Particle.BaseVelocity = bLocal ? FromWorld.TransformVector(ReflectedVelocity) : ReflectedVelocity;
					const FVector Bounce = Reflected(Travel, Result.Normal) * Damping * (1.f - Result.Time);
					if (bLocal)
						Particle.Location = FromWorld.TransformPosition(Location + Bounce);
					else
						Particle.Location += Bounce;
					// The collision module consumed this tick's displacement.
					Particle.Velocity = FVector::ZeroVector;
				}
				else
				{
					Particle.Location = bLocal ? FromWorld.TransformPosition(Result.Location) : Result.Location;

					switch (CollisionCompletionOption)
					{
					case EParticleCollisionComplete::EPCC_Kill:
						KILL_CURRENT_PARTICLE;
						break;
					case EParticleCollisionComplete::EPCC_Freeze:
						Particle.Flags |= STATE_Particle_Freeze;
						break;
					case EParticleCollisionComplete::EPCC_HaltCollisions:
						Particle.Flags |= STATE_Particle_IgnoreCollisions;
						break;
					case EParticleCollisionComplete::EPCC_FreezeTranslation:
						Particle.Flags |= STATE_Particle_FreezeTranslation;
						break;
					case EParticleCollisionComplete::EPCC_FreezeRotation:
						Particle.Flags |= STATE_Particle_FreezeRotation;
						break;
					case EParticleCollisionComplete::EPCC_FreezeMovement:
						Particle.Flags |= STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation;
						break;
					default:
						break;
					}
				}
			}

			END_UPDATE_LOOP;
		}
	}
}

uint32 UParticleModuleCollision::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	return sizeof(FParticleCollisionPayload);
}