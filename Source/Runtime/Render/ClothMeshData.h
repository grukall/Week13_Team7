#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Render/Vertex.h"
#include "Render/Buffer.h"

// 천 한 장을 그리는 데 필요한 렌더 자원. 시뮬레이션(NvCloth)은 모르고, 엔진 타입만 쓴다.
// UClothComponent가 천마다 하나씩 들고 있고, 같은 인덱스의 FClothInstance와 짝을 이룬다.
// TUniquePtr 때문에 복사할 수 없으므로 배열에 넣을 때는 std::move를 쓴다.
struct FClothMeshData
{
	TArray<FVertex> Vertices;					// CPU 정점 2N개. Position·Normal은 매 프레임 갱신, UV는 고정
	TArray<uint32>  Indices;					// 두 겹 삼각형 점 번호 3개씩. IB 생성과 법선 계산에 쓴다
	uint32 NumSimVertices = 0;					// N. 시뮬레이션 파티클 수 (= 앞면 정점 수)
	TUniquePtr<FVertexBuffer> VertexBuffer;		// Dynamic. 매 프레임 Vertices를 올린다
	TUniquePtr<FIndexBuffer>  IndexBuffer;		// Static. 연결 구조는 바뀌지 않으므로 한 번만 만든다

	void InitTwoSided(const TArray<FVector>& Positions, const TArray<FVector2>& UVs, const TArray<uint32>& SimIndices)
	{
		const uint32 N = Positions.Num();
		NumSimVertices = N;

		Vertices.SetNum(N * 2);
		for (uint32 j = 0; j < N; ++j)
		{
			Vertices[j].Position = Positions[j];
			Vertices[j].UV = UVs[j];
			Vertices[j + N].Position = Positions[j];
			Vertices[j + N].UV = UVs[j];			// 뒷면도 같은 UV (텍스처가 뒤에서 보면 좌우 반전됨)
		}

		Indices.Reset();
		Indices.Reserve(SimIndices.Num() * 2);
		for (int32 i = 0; i + 2 < SimIndices.Num(); i += 3)		// 앞면: 원래 순서
		{
			Indices.Add(SimIndices[i]); Indices.Add(SimIndices[i + 1]); Indices.Add(SimIndices[i + 2]);
		}
		for (int32 i = 0; i + 2 < SimIndices.Num(); i += 3)		// 뒷면: 감는 순서 반대 (b, c 교환)
		{
			Indices.Add(SimIndices[i] + N); Indices.Add(SimIndices[i + 2] + N); Indices.Add(SimIndices[i + 1] + N);
		}
	}

	void FinalizeTwoSided(float Thickness)
	{
		const uint32 N = NumSimVertices;
		for (uint32 j = 0; j < N; ++j)
		{
			Vertices[j + N].Position = Vertices[j].Position;	// 오프셋 전에는 두 겹이 같은 위치
		}

		BuildNormal();		// 앞면 +n, 뒷면 -n

		const float HalfThickness = Thickness > 0.0f ? Thickness * 0.5f : 0.0f;
		if (HalfThickness > 0.0f)
		{
			for (FVertex& Vertex : Vertices)
			{
				Vertex.Position += Vertex.Normal * HalfThickness;	// 각자 자기 법선 쪽으로 → 두 겹 사이 간격 = Thickness
			}
		}

		BuildTangentSpace();	// Normal을 읽으므로 마지막에
	}

	// 시뮬레이션 공간(천 R·T만 벗긴 공간, 스케일이 모양에 구워짐) → 렌더 로컬(GPU가 S·R·T를 곱하는 공간).
	// FinalizeTwoSided 다음, GPU 업로드 전에 호출한다. 두께 오프셋·법선은 시뮬레이션 공간에서 이미 월드 단위로 계산된 상태여야 한다.
	//  - 위치·탄젠트: GPU가 World(×S)를 곱하므로 미리 ÷S
	//  - 법선: GPU가 NormalMatrix(World⁻¹ᵀ, 법선에 ÷S 효과)를 곱하므로 미리 ×S
	void ConvertSimToRenderSpace(const FVector& SimScale)
	{
		constexpr float VectorToleranceSquared{ 1.0e-12f };
		for (FVertex& Vertex : Vertices)
		{
			Vertex.Position = Vertex.Position / SimScale;
			Vertex.Normal = (Vertex.Normal * SimScale).GetSafeNormal(VectorToleranceSquared, FVector(0.0f, 0.0f, 1.0f));
			const FVector Tangent = (FVector(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z) / SimScale).GetSafeNormal(VectorToleranceSquared, FVector(1.0f, 0.0f, 0.0f));
			Vertex.Tangent = FVector4{ Tangent, Vertex.Tangent.W };	// W(손 방향)는 양수 스케일에서 바뀌지 않는다
		}
	}

	void BuildTangentSpace() {
		const int32 VertexCount{ Vertices.Num() };
		const int32 IndexCount{ Indices.Num() };
		constexpr float UVTolerance{ 1.0e-8f };
		constexpr float VectorToleranceSquared{ 1.0e-12f };

		TArray<FVector> TangentSums{};
		TArray<FVector> BitangentSums{};
		TangentSums.SetNum(VertexCount);
		BitangentSums.SetNum(VertexCount);
		// 삼각형마다 Tangent, Bitangent를 계산하고 각 정점에 합산
		for (int32 Index{ 0 }; Index + 2 < IndexCount; Index += 3) {
			const uint32 IndexA{ Indices[Index] };
			const uint32 IndexB{ Indices[Index + 1] };
			const uint32 IndexC{ Indices[Index + 2] };

			const FVertex& VertexA{ Vertices[IndexA] };
			const FVertex& VertexB{ Vertices[IndexB] };
			const FVertex& VertexC{ Vertices[IndexC] };

			const FVector Edge1{ VertexB.Position - VertexA.Position };
			const FVector Edge2{ VertexC.Position - VertexA.Position };
			const FVector2 DeltaUV1{ VertexB.UV - VertexA.UV };
			const FVector2 DeltaUV2{ VertexC.UV - VertexA.UV };

			const float Determinant{ DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X };
			if (std::abs(Determinant) <= UVTolerance) {
				continue;
			}

			const FVector Tangent{ (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) / Determinant };
			const FVector Bitangent{ (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) / Determinant };

			TangentSums[IndexA] += Tangent;
			TangentSums[IndexB] += Tangent;
			TangentSums[IndexC] += Tangent;

			BitangentSums[IndexA] += Bitangent;
			BitangentSums[IndexB] += Bitangent;
			BitangentSums[IndexC] += Bitangent;
		}

		for (int32 Index{ 0 }; Index < VertexCount; ++Index) {
			FVertex& Vertex{ Vertices[Index] };
			const FVector Normal{ Vertex.Normal.GetSafeNormal(VectorToleranceSquared) };

			if (Normal.SizeSquared() <= VectorToleranceSquared) {
				Vertex.Tangent = FVector4{ 1.0f, 0.0f, 0.0f, 1.0f };
				continue;
			}

			FVector Tangent{ TangentSums[Index] - Normal * Normal.Dot(TangentSums[Index]) };

			if (Tangent.SizeSquared() <= VectorToleranceSquared) {
				const FVector Axis{ std::abs(Normal.X) < 0.9f ? FVector{ 1.0f, 0.0f, 0.0f } : FVector{ 0.0f, 1.0f, 0.0f } };
				Tangent = Axis - Normal * Normal.Dot(Axis);
			}

			Tangent = Tangent.GetSafeNormal(VectorToleranceSquared);

			const float Handedness{ Normal.Cross(Tangent).Dot(BitangentSums[Index]) < 0.0f ? -1.0f : 1.0f };
			Vertex.Tangent = FVector4{ Tangent, Handedness };
		}
	}

	void BuildNormal()
	{
		constexpr float VectorToleranceSquared{ 1.0e-12f };
		const int32 VertexCount{ Vertices.Num() };
		const int32 IndexCount{ Indices.Num() };

		// Normal을 초기에 0으로 초기화 
		for (int32 i = 0; i < VertexCount; i++)
		{
			Vertices[i].Normal = FVector(0.0f, 0.0f, 0.0f);
		}

		// 2. 삼각형마다 면 법선을 세 정점에 더한다. 정규화하지 않은 외적이라 넓은 삼각형일수록 비중이 크다 (넓이 가중치)
		//    외적 순서가 법선 방향을 정한다. 렌더링에서 조명이 뒤집혀 보이면 Edge1, Edge2 순서를 바꾼다
		for (int32 i = 0; i + 2 < IndexCount; i += 3)
		{
			const uint32 IndexA{ Indices[i] };
			const uint32 IndexB{ Indices[i + 1] };
			const uint32 IndexC{ Indices[i + 2] };
			if (IndexA >= static_cast<uint32>(VertexCount) || IndexB >= static_cast<uint32>(VertexCount) || IndexC >= static_cast<uint32>(VertexCount))
			{
				continue;
			}

			const FVector Edge1{ Vertices[IndexB].Position - Vertices[IndexA].Position };
			const FVector Edge2{ Vertices[IndexC].Position - Vertices[IndexA].Position };
			const FVector FaceNormal{ FVector::Cross(Edge1, Edge2) };

			Vertices[IndexA].Normal += FaceNormal;
			Vertices[IndexB].Normal += FaceNormal;
			Vertices[IndexC].Normal += FaceNormal;
		}

		for (int32 i = 0; i < VertexCount; i++)
		{
			Vertices[i].Normal = Vertices[i].Normal.GetSafeNormal(VectorToleranceSquared, FVector(0.0f, 0.0f, 1.0f));
		}
	}
};
