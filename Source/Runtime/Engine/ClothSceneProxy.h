#pragma once

#include "PrimitiveSceneProxy.h"

class UClothComponent;

// 천 전용 Proxy. 천은 메시 에셋(GetMesh·RenderState)이 없고 매 프레임 컴포넌트가 VB를 갱신하므로
// "직접 패킷을 만들 수 있는 물체"(HasDynamicMeshElements)로 알려야 그림자 패스가 Caster로 받아준다 (ShadowSetup AddInteractingPrimitives).
// 렌더 데이터(ClothMeshDatas)는 NvCloth 천과 수명을 맞추기 위해 컴포넌트가 소유하고,
// 패킷은 기본 GetDynamicMeshElements → UClothComponent::SubmitToRenderQueue가 만든다.
class FClothSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FClothSceneProxy(UPrimitiveComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
	{
	}

	bool HasDynamicMeshElements() const override { return true; }
	bool SupportsDynamicShadowDepth() const override { return true; }	// FVertex VB/IB + Proxy 행렬만 쓰므로 RenderState 없이 그림자 깊이에 그려도 된다
};
