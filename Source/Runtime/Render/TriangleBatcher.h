#pragma once

#include "Buffer.h"
#include "Vertex.h"
#include "PipelineState.h"

// 반투명 면을 매 프레임 쌓아 한 번에 그린다 (디버그 · 에디터 표시용).
// FLineBatcher와 같은 셰이더 · 정점 형식을 쓰고 파이프라인 설정만 다르다
//   TriangleList  : 면을 그린다
//   AlphaBlend    : 정점 색의 알파가 투명도 (예: 0.3)
//   Depth ReadOnly: 메시 뒤면 가려지지만 깊이는 쓰지 않아, 겹친 반투명 면끼리 서로 가리지 않는다
//   SolidNone     : 뒷면도 그린다 (콘 안쪽, 부채꼴 뒷면)
// 사용 순서: BeginFrame → Add... → OnRender(ViewProjection)
class FTriangleBatcher
{
public:
	FTriangleBatcher() = default;
	~FTriangleBatcher();

	bool Init();
	void BeginFrame();

	void AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color);
	// A-B-C-D 순서(둘레를 따라)의 사각형. 삼각형 두 개로 나눈다
	void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector4& Color);
	// Center를 꼭짓점으로 Points[i] - Points[i+1]를 잇는 부채꼴. 원뿔 옆면 · 원판에도 쓴다
	void AddFan(const FVector& Center, const TArray<FVector>& Points, const FVector4& Color);

	void OnRender(const FMatrix& ViewProjection);

private:
	FPipelineState PipelineState;
	FLineBatchVertex* VertexBufferBase = nullptr;
	FLineBatchVertex* VertexBufferPtr = nullptr;
	uint32 VertexCount = 0;
	uint32 MaxVertices = 0;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FConstantBuffer> CB;
};
