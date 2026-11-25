#pragma once

#include "Components/PrimitiveComponent.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Box.h"
#include "TireSkidMarkComponent.generated.h"

class UMaterial;
class UTexture2D;

struct FSkidMarkQuad
{
	FVector V0 = FVector::ZeroVector; // PrevLeft
	FVector V1 = FVector::ZeroVector; // PrevRight
	FVector V2 = FVector::ZeroVector; // CurrRight
	FVector V3 = FVector::ZeroVector; // CurrLeft
	float U0 = 0.0f;
	float U1 = 0.0f;
	float Alpha0 = 0.0f;
	float Alpha1 = 0.0f;
	float SpawnTime = 0.0f;
};

struct FWheelSkidTrack
{
	bool bActive = false;
	bool bHasPrevEdges = false;
	FVector PrevCenter = FVector::ZeroVector;
	FVector PrevLeft = FVector::ZeroVector;
	FVector PrevRight = FVector::ZeroVector;
	float PrevAlpha = 0.0f;
	float AccumU = 0.0f;
};

UCLASS()
class UTireSkidMarkComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UTireSkidMarkComponent();
	~UTireSkidMarkComponent() override = default;

	void InitializeTracks(int32 NumWheels);
	void UpdateWheelTrack(
		int32 WheelIndex,
		bool bSkidding,
		const FVector& ContactPoint,
		const FVector& ContactNormal,
		float WheelWidth,
		float Intensity);
	void ClearMarks();

	void TickComponent(float DeltaTime) override;
	void OnUnregister() override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

	UMaterial* GetOrCreateSkidMaterial();

	const TArray<FSkidMarkQuad>& GetQuads() const { return Quads; }
	int32 GetQuadHead() const { return QuadHead; }
	int32 GetQuadCount() const { return QuadCount; }
	float GetElapsedTime() const { return ElapsedTime; }

	static constexpr int32 MaxSkidQuads = 2048;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 1.0, ClampMax = 120.0, Delta = 1.0))
	float MarkLifetime = 15.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.1, ClampMax = 10.0, Delta = 0.5))
	float FadeOutDuration = 3.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.05, ClampMax = 2.0, Delta = 0.05))
	float MinSegmentLength = 0.2f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.005, ClampMax = 0.1, Delta = 0.005))
	float GroundOffset = 0.02f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.1, ClampMax = 1.0, Delta = 0.05))
	float MaxOpacity = 0.78f;

	UPROPERTY(EditAnywhere)
	FString SkidTexturePath = "Assets/Textures/T_TireSkidMark.png";

private:
	void PushQuad(const FSkidMarkQuad& Quad);
	void RebuildWorldBounds();

	TArray<FWheelSkidTrack> Tracks;
	TArray<FSkidMarkQuad> Quads;
	int32 QuadHead = 0;
	int32 QuadCount = 0;

	float ElapsedTime = 0.0f;
	bool bHasWorldBounds = false;
	FBox CachedWorldBounds{ FVector::ZeroVector, FVector::ZeroVector };

	UMaterial* SkidMaterial = nullptr;
};
