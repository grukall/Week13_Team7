#include "EnginePCH.h"
#include "Components/TireSkidMarkComponent.h"
#include "Asset/AssetManager.h"
#include "Engine/ParticleSystemSceneProxy.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Particles/ParticleHelper.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Render/Texture2D.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>

namespace
{
	class FTireSkidMarkSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		explicit FTireSkidMarkSceneProxy(UTireSkidMarkComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
		{
		}

		void UpdateRenderState() override
		{
			RenderState.reset();
			UpdateCommonFlags();
		}

		bool HasDynamicMeshElements() const override { return true; }
		bool SupportsDynamicShadowDepth() const override { return false; }

		void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override
		{
			UTireSkidMarkComponent* Comp = static_cast<UTireSkidMarkComponent*>(GetComponent());
			if (!Comp)
			{
				return;
			}

			const int32 QuadCount = Comp->GetQuadCount();
			if (QuadCount <= 0)
			{
				return;
			}

			UMaterial* Material = Comp->GetOrCreateSkidMaterial();
			if (!Material)
			{
				return;
			}

			const uint32 CountU32 = static_cast<uint32>(QuadCount);
			if (!EnsureBuffers(CountU32))
			{
				return;
			}

			FParticleTrailVertex* Dst = static_cast<FParticleTrailVertex*>(RenderCommand::MapWriteDiscard(VertexBuffer.get()));
			if (!Dst)
			{
				return;
			}

			const TArray<FSkidMarkQuad>& Quads = Comp->GetQuads();
			const int32 Capacity = Quads.Num();
			const int32 Head = Comp->GetQuadHead();
			const float Now = Comp->GetElapsedTime();
			const float Lifetime = std::max(Comp->MarkLifetime, 0.1f);
			const float FadeDur = std::max(Comp->FadeOutDuration, 0.05f);

			uint32 Written = 0;
			for (int32 i = 0; i < QuadCount; ++i)
			{
				const FSkidMarkQuad& Q = Quads[(Head + i) % Capacity];
				const float Remaining = Lifetime - (Now - Q.SpawnTime);
				if (Remaining <= 0.0f)
				{
					continue;
				}

				const float LifeFade = std::clamp(Remaining / FadeDur, 0.0f, 1.0f);
				const FLinearColor Color0(0.06f, 0.06f, 0.07f, std::clamp(Q.Alpha0 * LifeFade, 0.0f, 1.0f));
				const FLinearColor Color1(0.06f, 0.06f, 0.07f, std::clamp(Q.Alpha1 * LifeFade, 0.0f, 1.0f));

				FParticleTrailVertex V;
				V.Position = Q.V0; V.UV = FVector2(Q.U0, 0.0f); V.Color = Color0; Dst[Written * 4 + 0] = V;
				V.Position = Q.V1; V.UV = FVector2(Q.U0, 1.0f); V.Color = Color0; Dst[Written * 4 + 1] = V;
				V.Position = Q.V2; V.UV = FVector2(Q.U1, 1.0f); V.Color = Color1; Dst[Written * 4 + 2] = V;
				V.Position = Q.V3; V.UV = FVector2(Q.U1, 0.0f); V.Color = Color1; Dst[Written * 4 + 3] = V;
				++Written;
			}

			RenderCommand::Unmap(VertexBuffer.get());
			if (Written == 0)
			{
				return;
			}

			Constants.CameraRight = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Constants.CameraUp = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Constants.SubUVInfo = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // z=0: AlphaBlend

			FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
			Packet.Proxy = nullptr;
			Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
			Packet.VertexBuffer = VertexBuffer.get();
			Packet.IndexBuffer = IndexBuffer.get();
			Packet.Material = Material;
			Packet.MaterialParamData = &Constants;
			Packet.MaterialParamDataSize = sizeof(FParticleSpriteConstants);
			Packet.StartIndex = 0;
			Packet.IndexCount = Written * 6;

			const FVector Delta = GetBounds().Center - View.CameraPosition;
			Packet.CameraToParticleDistance = Delta.Dot(Delta);
			Packet.TranslucencySortPriority = -10; // 타이어 연기(0)보다 먼저 그려 바닥에 깔림
		}

	private:
		bool EnsureBuffers(uint32 QuadCount)
		{
			if (VertexBuffer && IndexBuffer && CapacityQuads >= QuadCount)
			{
				return true;
			}

			uint32 NewCap = std::max(128u, CapacityQuads);
			while (NewCap < QuadCount)
			{
				NewCap *= 2;
			}
			NewCap = std::min(NewCap, static_cast<uint32>(UTireSkidMarkComponent::MaxSkidQuads));

			VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
				NewCap * 4 * sizeof(FParticleTrailVertex),
				sizeof(FParticleTrailVertex));

			TArray<uint32> Indices;
			Indices.Reserve(NewCap * 6);
			for (uint32 q = 0; q < NewCap; ++q)
			{
				const uint32 Base = q * 4;
				Indices.Add(Base + 0);
				Indices.Add(Base + 1);
				Indices.Add(Base + 2);
				Indices.Add(Base + 0);
				Indices.Add(Base + 2);
				Indices.Add(Base + 3);
			}
			IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Indices.GetData(), Indices.Num());

			const bool bOk = VertexBuffer && VertexBuffer->GetBuffer() && IndexBuffer && IndexBuffer->GetBuffer();
			CapacityQuads = bOk ? NewCap : 0;
			return bOk;
		}

		TUniquePtr<FVertexBuffer> VertexBuffer;
		TUniquePtr<FIndexBuffer> IndexBuffer;
		uint32 CapacityQuads = 0;
		FParticleSpriteConstants Constants{};
	};
}

UTireSkidMarkComponent::UTireSkidMarkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bCastShadow = false;
	bReceivesDecals = false;
	TranslucencySortPriority = -10;
	Quads.SetNum(MaxSkidQuads);
}

void UTireSkidMarkComponent::InitializeTracks(int32 NumWheels)
{
	Tracks.Reset();
	if (NumWheels > 0)
	{
		Tracks.SetNum(NumWheels);
	}
	ClearMarks();
}

void UTireSkidMarkComponent::ClearMarks()
{
	QuadHead = 0;
	QuadCount = 0;
	bHasWorldBounds = false;
	CachedWorldBounds = FBox{ FVector::ZeroVector, FVector::ZeroVector };
	for (FWheelSkidTrack& Track : Tracks)
	{
		Track = FWheelSkidTrack{};
	}
	OnTransformDirty();
}

void UTireSkidMarkComponent::PushQuad(const FSkidMarkQuad& Quad)
{
	if (Quads.IsEmpty())
	{
		Quads.SetNum(MaxSkidQuads);
	}

	const int32 Capacity = Quads.Num();
	if (QuadCount < Capacity)
	{
		const int32 Tail = (QuadHead + QuadCount) % Capacity;
		Quads[Tail] = Quad;
		++QuadCount;
	}
	else
	{
		Quads[QuadHead] = Quad;
		QuadHead = (QuadHead + 1) % Capacity;
	}

	auto ExpandPoint = [this](const FVector& P)
	{
		if (!bHasWorldBounds)
		{
			CachedWorldBounds = FBox{ P, P };
			bHasWorldBounds = true;
		}
		else
		{
			CachedWorldBounds.Min.X = std::min(CachedWorldBounds.Min.X, P.X);
			CachedWorldBounds.Min.Y = std::min(CachedWorldBounds.Min.Y, P.Y);
			CachedWorldBounds.Min.Z = std::min(CachedWorldBounds.Min.Z, P.Z);
			CachedWorldBounds.Max.X = std::max(CachedWorldBounds.Max.X, P.X);
			CachedWorldBounds.Max.Y = std::max(CachedWorldBounds.Max.Y, P.Y);
			CachedWorldBounds.Max.Z = std::max(CachedWorldBounds.Max.Z, P.Z);
		}
	};

	ExpandPoint(Quad.V0);
	ExpandPoint(Quad.V1);
	ExpandPoint(Quad.V2);
	ExpandPoint(Quad.V3);
	OnTransformDirty();
}

void UTireSkidMarkComponent::RebuildWorldBounds()
{
	bHasWorldBounds = false;
	if (QuadCount <= 0 || Quads.IsEmpty())
	{
		CachedWorldBounds = FBox{ FVector::ZeroVector, FVector::ZeroVector };
		return;
	}

	const int32 Capacity = Quads.Num();
	for (int32 i = 0; i < QuadCount; ++i)
	{
		const FSkidMarkQuad& Q = Quads[(QuadHead + i) % Capacity];
		const FVector Pts[4] = { Q.V0, Q.V1, Q.V2, Q.V3 };
		for (const FVector& P : Pts)
		{
			if (!bHasWorldBounds)
			{
				CachedWorldBounds = FBox{ P, P };
				bHasWorldBounds = true;
			}
			else
			{
				CachedWorldBounds.Min.X = std::min(CachedWorldBounds.Min.X, P.X);
				CachedWorldBounds.Min.Y = std::min(CachedWorldBounds.Min.Y, P.Y);
				CachedWorldBounds.Min.Z = std::min(CachedWorldBounds.Min.Z, P.Z);
				CachedWorldBounds.Max.X = std::max(CachedWorldBounds.Max.X, P.X);
				CachedWorldBounds.Max.Y = std::max(CachedWorldBounds.Max.Y, P.Y);
				CachedWorldBounds.Max.Z = std::max(CachedWorldBounds.Max.Z, P.Z);
			}
		}
	}
}

void UTireSkidMarkComponent::UpdateWheelTrack(
	int32 WheelIndex,
	bool bSkidding,
	const FVector& ContactPoint,
	const FVector& ContactNormal,
	float WheelWidth,
	float Intensity)
{
	if (!Tracks.IsValidIndex(WheelIndex))
	{
		return;
	}

	FWheelSkidTrack& Track = Tracks[WheelIndex];
	if (!bSkidding)
	{
		Track.bActive = false;
		Track.bHasPrevEdges = false;
		return;
	}

	FVector Normal = ContactNormal;
	if (Normal.Length() < 1e-4f)
	{
		Normal = UpVector;
	}
	else
	{
		Normal = Normal.Normalized();
	}

	const FVector CurrCenter = ContactPoint + Normal * GroundOffset;
	const float TargetAlpha = std::clamp(Intensity, 0.0f, 1.0f) * MaxOpacity;

	if (!Track.bActive)
	{
		Track.bActive = true;
		Track.bHasPrevEdges = false;
		Track.PrevCenter = CurrCenter;
		Track.PrevAlpha = 0.0f; // 시작 끝단 자연 페이드인
		Track.AccumU = 0.0f;
		return;
	}

	const FVector Delta = CurrCenter - Track.PrevCenter;
	const float Dist = Delta.Length();

	// 순간이동 등으로 비정상적으로 먼 거리 이동 시 트랙 끊기
	if (Dist > 5.0f)
	{
		Track.bActive = false;
		Track.bHasPrevEdges = false;
		return;
	}

	if (Dist < MinSegmentLength)
	{
		return;
	}

	const FVector Tangent = Delta * (1.0f / Dist);
	FVector SideDir = FVector::Cross(Normal, Tangent);
	if (SideDir.Length() < 1e-4f)
	{
		return;
	}
	SideDir = SideDir.Normalized();

	const float HalfWidth = std::max(WheelWidth * 0.5f, 0.08f);
	const FVector Side = SideDir * HalfWidth;

	if (!Track.bHasPrevEdges)
	{
		Track.PrevLeft = Track.PrevCenter - Side;
		Track.PrevRight = Track.PrevCenter + Side;
		Track.bHasPrevEdges = true;
	}

	const FVector CurrLeft = CurrCenter - Side;
	const FVector CurrRight = CurrCenter + Side;
	const float NextU = Track.AccumU + Dist * 1.5f;

	FSkidMarkQuad Quad;
	Quad.V0 = Track.PrevLeft;
	Quad.V1 = Track.PrevRight;
	Quad.V2 = CurrRight;
	Quad.V3 = CurrLeft;
	Quad.U0 = Track.AccumU;
	Quad.U1 = NextU;
	Quad.Alpha0 = Track.PrevAlpha;
	Quad.Alpha1 = TargetAlpha;
	Quad.SpawnTime = ElapsedTime;
	PushQuad(Quad);

	Track.PrevCenter = CurrCenter;
	Track.PrevLeft = CurrLeft;
	Track.PrevRight = CurrRight;
	Track.PrevAlpha = TargetAlpha;
	Track.AccumU = NextU;
}

void UTireSkidMarkComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	ElapsedTime += DeltaTime;

	bool bExpiredAny = false;
	const int32 Capacity = Quads.Num();
	while (QuadCount > 0 && Capacity > 0)
	{
		const FSkidMarkQuad& Oldest = Quads[QuadHead];
		if ((ElapsedTime - Oldest.SpawnTime) < MarkLifetime)
		{
			break;
		}
		QuadHead = (QuadHead + 1) % Capacity;
		--QuadCount;
		bExpiredAny = true;
	}

	if (bExpiredAny)
	{
		RebuildWorldBounds();
		OnTransformDirty();
	}
}

void UTireSkidMarkComponent::OnUnregister()
{
	ClearMarks();
	Super::OnUnregister();
}

FPrimitiveSceneProxy* UTireSkidMarkComponent::CreateSceneProxy()
{
	return new FTireSkidMarkSceneProxy(this);
}

FBox UTireSkidMarkComponent::CalcLocalBounds() const
{
	if (!bHasWorldBounds)
	{
		return FBox{ FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f) };
	}

	const FMatrix WorldToLocal = GetWorldMatrix().Inverse();
	const FVector& Min = CachedWorldBounds.Min;
	const FVector& Max = CachedWorldBounds.Max;
	const FVector Corners[8] =
	{
		FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z),
		FVector(Min.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z),
		FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z),
		FVector(Min.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z)
	};

	FVector LocalMin = WorldToLocal.TransformPosition(Corners[0]);
	FVector LocalMax = LocalMin;
	for (int32 i = 1; i < 8; ++i)
	{
		const FVector P = WorldToLocal.TransformPosition(Corners[i]);
		LocalMin.X = std::min(LocalMin.X, P.X);
		LocalMin.Y = std::min(LocalMin.Y, P.Y);
		LocalMin.Z = std::min(LocalMin.Z, P.Z);
		LocalMax.X = std::max(LocalMax.X, P.X);
		LocalMax.Y = std::max(LocalMax.Y, P.Y);
		LocalMax.Z = std::max(LocalMax.Z, P.Z);
	}

	return FBox{ LocalMin - FVector(0.25f, 0.25f, 0.25f), LocalMax + FVector(0.25f, 0.25f, 0.25f) };
}

UMaterial* UTireSkidMarkComponent::GetOrCreateSkidMaterial()
{
	if (!SkidMaterial)
	{
		SkidMaterial = NewObject<UMaterial>(this);
		SkidMaterial->PSOType = EPSOType::ParticleTrail_AlphaBlend;
		SkidMaterial->BlendState = EBlendState::AlphaBlend;
		SkidMaterial->DepthStencilState = EDepthStencilState::ReadOnly;
		SkidMaterial->SamplerState = ESamplerState::LinearWrapUClampV;
	}

	UTexture2D* Tex = nullptr;
	if (!SkidTexturePath.empty())
	{
		Tex = UAssetManager::Get().LoadTexture(SkidTexturePath);
	}
	if (!Tex)
	{
		Tex = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
	}

	if (Tex)
	{
		if (SkidMaterial->Textures.IsEmpty())
		{
			SkidMaterial->Textures.Add(Tex);
		}
		else
		{
			SkidMaterial->Textures[0] = Tex;
		}
	}

	return SkidMaterial;
}
