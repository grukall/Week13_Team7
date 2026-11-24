#pragma once

#include "Math/Frustum.h"
#include "Render/MeshRenderData.h"
#include "Render/RenderPacket.h"

class FScene;
class UPrimitiveComponent;
class UStaticMesh;
class UMaterial;
class FRenderQueue;
struct FViewContext;

class FPrimitiveSceneProxy
{
public:
	explicit FPrimitiveSceneProxy(UPrimitiveComponent* InComponent);
	
	virtual ~FPrimitiveSceneProxy();
	virtual void UpdateRenderState();

	virtual bool HasDynamicMeshElements() const { return false; }
	virtual void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View);
	// 메시 캐시(RenderState) 없이도 GetDynamicMeshElements 패킷을 그림자 깊이 패스가 그대로 그려도 되는가.
	// 패킷이 FVertex VB/IB와 Proxy 행렬만 쓰는 경우에만 true (천). 파티클처럼 뷰 의존·전용 정점이면 false로 둔다
	virtual bool SupportsDynamicShadowDepth() const { return false; }

	bool IsSkinned() const { return bSkinned; }
	
	void UpdateTransform();
	void OnMeshRenderDataChanged();
	void OnMeshDestroyed();

	UPrimitiveComponent* GetComponent() const { return Component; }
	const FAABB& GetBounds() const { return Bounds; }
	const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
	const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
	const FLODSphere& GetLODSphere() const { return LODSphere; }
	UStaticMesh* GetMesh() const { return Mesh; }
	bool IsVisible() const { return bVisible; }
	bool IsShown(bool bGameView) const { return bVisible && !(bGameView && bHiddenInGame); }
	uint8 GetLODCount() const { return RenderState ? RenderState->LODCount : 1; }
	const FMeshRenderState* GetRenderState() const { return RenderState.get(); }
	const float* GetLODThresholdsSq() const { return RenderState->LODThresholdSq; }
	const FCachedMeshLOD& GetLOD(uint32 Index) const { return RenderState->LODs[Index]; }
	const FCachedMeshSection& GetSection(uint32 Index) const { return RenderState->Sections[Index]; }
	FScene* GetScene() const { return Scene; }

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceivesDecals = true;
protected:
	bool bSkinned = false;
	TSharedPtr<const FMeshRenderState> RenderState;
	void UpdateCommonFlags();
private:
	friend class FScene;
	FScene* Scene = nullptr;
	UPrimitiveComponent* Component = nullptr;
	UStaticMesh* Mesh = nullptr;
	FMatrix LocalToWorld;
	FMatrix WorldToLocal;
	FAABB Bounds;
	FLODSphere LODSphere;
	int32 PackedIndex = INDEX_NONE;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
	bool bVisible = true;
	bool bHiddenInGame = false;
};
