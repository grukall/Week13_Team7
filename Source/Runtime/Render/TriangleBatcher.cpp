#include "EnginePCH.h"
#include "TriangleBatcher.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"

FTriangleBatcher::~FTriangleBatcher()
{
	delete[] VertexBufferBase;
}

bool FTriangleBatcher::Init()
{
	// 위치를 ViewProj로 옮기고 정점 색을 그대로 출력하는 LineBatcher 셰이더를 같이 쓴다
	PipelineState.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/BoundingBoxShader.hlsl");
	PipelineState.Topology = EPrimitiveTopology::TriangleList;
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::ReadOnly;
	PipelineState.RasterizerState = ERasterizerState::SolidNone;

	// 삼각형 하나에 정점 3개. 캡슐 하나 ≈ 300 삼각형, 바디 30개 ≈ 27000 정점이라 여유 있게 잡는다
	MaxVertices = 65536 * 3;
	VertexBufferBase = new FLineBatchVertex[MaxVertices];

	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FLineBatchVertex) * MaxVertices, sizeof(FLineBatchVertex));
	CB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));

	BeginFrame();
	return VertexBuffer && CB;
}

void FTriangleBatcher::BeginFrame()
{
	VertexBufferPtr = VertexBufferBase;
	VertexCount = 0;
}

void FTriangleBatcher::AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color)
{
	if (!VertexBufferBase || VertexCount + 3 > MaxVertices)
		return;   // 넘치면 버린다

	*VertexBufferPtr++ = { A, FVector2(), Color };
	*VertexBufferPtr++ = { B, FVector2(), Color };
	*VertexBufferPtr++ = { C, FVector2(), Color };
	VertexCount += 3;
}

void FTriangleBatcher::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector4& Color)
{
	AddTriangle(A, B, C, Color);
	AddTriangle(A, C, D, Color);
}

void FTriangleBatcher::AddFan(const FVector& Center, const TArray<FVector>& Points, const FVector4& Color)
{
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		AddTriangle(Center, Points[i], Points[i + 1], Color);
	}
}

void FTriangleBatcher::OnRender(const FMatrix& ViewProjection)
{
	if (VertexCount == 0)
	{
		return;
	}

	FMatrix VP = ViewProjection;
	RenderCommand::UpdateBufferData(VertexBuffer.get(), VertexBufferBase, sizeof(FLineBatchVertex) * VertexCount);
	RenderCommand::UpdateBufferData(CB.get(), &VP, sizeof(FMatrix));

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex);
	RenderCommand::Draw(VertexCount, 0);
}
