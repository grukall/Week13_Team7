#include "EnginePCH.h"
#include "ClothComponent.h"
#include "Physics/NvClothManager.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"
#include <NvCloth/Factory.h> 
#include <NvCloth/Solver.h> 
#include <NvCloth/Cloth.h> 
#include <NvClothExt/ClothFabricCooker.h>
#include <algorithm>	// std::min
#include <cmath>		// std::isfinite

namespace
{
	FClothBuildInput MakeDefaultGridInput(int32 Rows, int32 Cols, float Spacing)		// 에셋 등록 전 Default로 사용하는 천의 FClothBuildInput 만드는 함수
	{
		FClothBuildInput Input;

		if (Rows < 2 || Cols < 2 || Spacing <= 0.0f)
		{
			return Input;
		}

		const int32 NumPoints  = Rows * Cols;
		const int32 NumIndices = (Rows - 1) * (Cols - 1) * 6;
		Input.Positions.Reserve(static_cast<uint32>(NumPoints));
		Input.InvMasses.Reserve(static_cast<uint32>(NumPoints));
		Input.UVs.Reserve(static_cast<uint32>(NumPoints));
		Input.Indices.Reserve(static_cast<uint32>(NumIndices));

		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				Input.Positions.Add(FVector(c * Spacing, r * Spacing, 0.0f));
				//Input.InvMasses.Add(1.0f);
				Input.InvMasses.Add(r == 0 ? 0.0f : 1.0f);	// 첫 줄(r == 0)만 고정점. 모든 점에 하나씩 넣어야 Positions와 개수가 맞는다
				Input.UVs.Add(FVector2(c / static_cast<float>(Cols - 1), r / static_cast<float>(Rows - 1)));	// 격자 전체를 0~1로 펼친 UV
			}
		}

		for (int32 r = 0; r < Rows - 1; ++r)
		{
			for (int32 c = 0; c < Cols - 1; ++c)
			{
				const uint32 a = static_cast<uint32>(r * Cols + c);
				const uint32 b = a + 1;
				const uint32 d = a + static_cast<uint32>(Cols);
				const uint32 e = d + 1;

				Input.Indices.Add(a); Input.Indices.Add(d); Input.Indices.Add(b);
				Input.Indices.Add(b); Input.Indices.Add(d); Input.Indices.Add(e);
			}
		}

		return Input;
	}
}

nv::cloth::ClothMeshDesc MakeClothMeshDesc(const FClothBuildInput& Input)
{
	nv::cloth::ClothMeshDesc outDesc;
	outDesc.points.data = Input.Positions.GetData();
	outDesc.points.stride = sizeof(FVector);
	outDesc.points.count = Input.Positions.Num();
	outDesc.invMasses.data = Input.InvMasses.GetData();
	outDesc.invMasses.stride = sizeof(float);
	outDesc.invMasses.count = Input.InvMasses.Num();
	outDesc.triangles.data = Input.Indices.GetData();
	outDesc.triangles.stride = sizeof(uint32) * 3;
	outDesc.triangles.count = Input.Indices.Num() / 3;

	static_assert(sizeof(FVector) == sizeof(physx::PxVec3), "FVector must match PxVec3 layout");

	return outDesc;
}

UClothComponent::UClothComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::PostPhysics;
}

UClothComponent::~UClothComponent()
{
}

void UClothComponent::OnRegister()
{
	Super::OnRegister();

	nv::cloth::Factory* Factory = FNvClothManager::Get().GetFactory();  // NvCloth용 관련 객체들 생성하기 위한 Factory
	if (Factory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Factory is null (NvClothManager not initialized)");
		return;
	}

	Solver = Factory->createSolver();	// 천 계산할 Solver 세팅 (컴포넌트의 모든 천이 공유)
	if (Solver == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] createSolver failed");
		return;
	}

	// 에셋 등록 전에는 기본 격자 천 하나를 추가한다. 에셋이 생기면 그 입력으로 CreateCloth를 호출한다.
	CreateCloth(MakeDefaultGridInput(DefaultGridRows, DefaultGridCols, DefaultGridSpacing));
	OnTransformDirty();		// Proxy가 천 생성 전에 만들어졌을 수 있으므로 초기 바운드를 반영

}

void UClothComponent::OnUnregister()
{
	for (FClothInstance& Instance : ClothInstances)
	{
		DestroyCloth(Instance);
	}
	ClothInstances.Reset();
	NV_CLOTH_DELETE(Solver);	// 천이 모두 빠진 뒤에 Solver 삭제
	Solver = nullptr;
	Super::OnUnregister();
}

// 입력 하나로 Fabric Cook → Cloth 생성·설정 → Solver에 추가 → ClothInstances에 보관. 실패하면 false
bool UClothComponent::CreateCloth(const FClothBuildInput& Input)
{
	if (Solver == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] CreateCloth called without Solver");
		return false;
	}
	nv::cloth::Factory* Factory = FNvClothManager::Get().GetFactory();
	if (Factory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Factory is null (NvClothManager not initialized)");
		return false;
	}
	if (Input.Positions.Num() == 0 || Input.InvMasses.Num() != Input.Positions.Num() || Input.UVs.Num() != Input.Positions.Num() || Input.Indices.Num() % 3 != 0)
	{
		HTR_LOG(Error, "[NvCloth] Cloth build input is invalid");
		return false;
	}

	nv::cloth::ClothMeshDesc ClothDesc = MakeClothMeshDesc(Input);	// Fabric Cook용 Desc

	physx::PxVec3 gravity(0.0f, 0.0f, -1.0f);  // 기본 중력 세팅
	nv::cloth::Vector<int32_t>::Type phaseTypeInfo;	// phaseType 배열을 저장, 해당 배열은 i번째 phase의 종류를 담고 있음 (선 종류 / vertical 등등)ㄱ

	TArray<physx::PxVec4> Particles;
	Particles.Reserve(Input.Positions.Num());
	for (int i = 0; i < Input.Positions.Num();i++)
	{
		FVector P = Input.Positions[i];
		Particles.Add(physx::PxVec4(P.X, P.Y, P.Z, Input.InvMasses[i]));		// 정점 xyz + 역질량 좌표 Particles 배열에 추가
	}
	nv::cloth::Range<const physx::PxVec4> ParticleRange(Particles.GetData(), Particles.GetData() + Particles.Num());		// Particle 배열 시작과 끝 지점 Range 구조체로 정의 (시작, 끝 지점 포인터 존재)
	

	
	nv::cloth::Fabric* Fabric = NvClothCookFabricFromMesh(Factory, ClothDesc, gravity, &phaseTypeInfo);
	if (Fabric == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Fabric cooking failed");
		return false;
	}

	nv::cloth::Cloth* Cloth = Factory->createCloth(ParticleRange, *Fabric);
	if (Cloth == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] createCloth failed");
		Fabric->decRefCount();		// 굽기에서 받은 참조(1)를 반납 → Fabric 삭제
		return false;
	}

	int32 PhaseNum = Fabric->getNumPhases();
	TArray<nv::cloth::PhaseConfig> Configs; 
	Configs.Reserve(PhaseNum);
	for (int i = 0;i < PhaseNum;i++)
	{
		nv::cloth::PhaseConfig confi = nv::cloth::PhaseConfig(i);
		confi.mStiffness = 1.0f;
		Configs.Add(confi);	//  Fabric의 Phase 만큼 PhaseConfig 세팅
	}
	Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(Configs.GetData(), Configs.GetData() + Configs.Num()));	// PhaseConfig 세팅
	Cloth->setDragCoefficient(0.2f);   // 공기 저항
	Cloth->setLiftCoefficient(0.2f);   // 양력 (천이 들썩이는 힘)

	FClothMeshData ClothData;	// Cloth Rendering용 데이터 생성 (앞면·뒷면 두 겹, 정점 2N개)
	ClothData.InitTwoSided(Input.Positions, Input.UVs, Input.Indices);
	ClothData.FinalizeTwoSided(RenderThickness);

	ClothData.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(ClothData.Indices.GetData(), ClothData.Indices.Num());
	ClothData.VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FVertex) * ClothData.Vertices.Num(), sizeof(FVertex));

	if (!ClothData.IndexBuffer || !ClothData.IndexBuffer->GetBuffer() || !ClothData.VertexBuffer || !ClothData.VertexBuffer->GetBuffer())
	{
		HTR_LOG(Error, "[NvCloth] Cloth render buffer creation failed");
		NV_CLOTH_DELETE(Cloth);		// 오류 발생시 생성한 cloth는 삭제
		Fabric->decRefCount();		// Fabric도 참조 반납
		return false;
	}

	RenderCommand::UpdateBufferData(ClothData.VertexBuffer.get(), ClothData.Vertices.GetData(), sizeof(FVertex) * ClothData.Vertices.Num());	// Vertex, IndexBuffer 생성및 Vertex 업데이트

	Solver->addCloth(Cloth);		// Buffer 채우기 및 모든 과정 종료 후 Solver에 Cloth 추가

	ClothInstances.Add(FClothInstance{ Fabric, Cloth, Input.Indices });
	ClothMeshDatas.Add(std::move(ClothData));
	return true;
}

void UClothComponent::DestroyCloth(FClothInstance& Instance)
{
	if (Instance.Cloth != nullptr)
	{
		if (Solver != nullptr)
		{
			Solver->removeCloth(Instance.Cloth);
		}
		NV_CLOTH_DELETE(Instance.Cloth);		// User가 초기에 지정한 deallocate 호출됨
		Instance.Cloth = nullptr;
	}
	if (Instance.Fabric != nullptr)
	{
		Instance.Fabric->decRefCount();	// Fabric의 경우 여러 Cloth가 공유할수 있기 때문에 참조Count로 관리, Fabric 안에서 자체적으로 참조수 = 0일때 delete 진행함
		Instance.Fabric = nullptr;
	}
}

void UClothComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (Solver == nullptr) return;
	if (!(DeltaTime > 0.0f) || !std::isfinite(DeltaTime)) return;
	float dt = std::min(DeltaTime, 1.0f / 30.0f);

	WindTime += DeltaTime;
	const float Gust = 1.0f + 0.5f * std::sin(WindTime * 2.0f) + 0.25f * std::sin(WindTime * 5.3f);	
	const physx::PxVec3 Wind(
		0.3f * BaseWind * std::sin(WindTime * 1.3f),	// X: 좌우 흔들림
		BaseWind * Gust,								// Y: 주 방향. 첫 줄(X축 방향)에 매달린 천의 면에 수직
		0.2f * BaseWind * std::sin(WindTime * 3.7f));	// Z: 위아래 들썩임
	for (FClothInstance& Instance : ClothInstances)
	{
		if (Instance.Cloth != nullptr)
		{
			Instance.Cloth->setWindVelocity(Wind);
			if (bEnableGravity)
			{
				Instance.Cloth->setGravity(physx::PxVec3(0, 0, -9.81));
			}
		}
	}

	if (!Solver->beginSimulation(dt)) return;
	for (int i = 0;i < Solver->getSimulationChunkCount();i++)		
	// Factory의 종류 (CPU,CUDA,GPU) 에 따라서 Solver가 한 프레임에 Simulation 돌릴 양을 얼마나 나눠서 돌릴지가 다름
	// CPU는 천 당 하나의 Chunk로 나눠서 돌림
	{
		Solver->simulateChunk(i);
	}
	Solver->endSimulation();

	for (int i = 0;i < ClothInstances.Num();i++)		// Simulation에 따라 업데이트 된 정점들 다시 세팅
	{
		const nv::cloth::Cloth& Cloth = *ClothInstances[i].Cloth;
		FClothMeshData& Data = ClothMeshDatas[i];
		{
			nv::cloth::MappedRange<const physx::PxVec4> Particles = Cloth.getCurrentParticles();
			const uint32 N = std::min(Particles.size(), Data.NumSimVertices);	// 앞면 [0, N)에만 파티클 위치를 쓴다
			for (uint32 j = 0; j < N; j++)
			{
				Data.Vertices[j].Position = FVector(Particles[j].x, Particles[j].y, Particles[j].z);
			}
		}	// Particles 소멸 → unlock
		Data.FinalizeTwoSided(RenderThickness);		// 뒷면 복사 → 법선 → 두께 오프셋 → 탄젠트

		void* Dst = RenderCommand::MapWriteDiscard(Data.VertexBuffer.get());	// 업데이트 된 정점들 GPU에 다시 기록
		if (Dst)
		{
			memcpy(Dst, Data.Vertices.GetData(), sizeof(FVertex) * Data.Vertices.Num());
			RenderCommand::Unmap(Data.VertexBuffer.get());
		}

	}

	OnTransformDirty();
}

UMaterial* UClothComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SlotIndex == 0 ? UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial") : nullptr;	// AssetManager::CreateDefaultMaterial에서 등록
}

FBox UClothComponent::CalcLocalBounds() const
{
	bool bHasBounds = false;
	FVector Min(0.0f), Max(0.0f);
	for (const FClothMeshData& Data : ClothMeshDatas)
	{
		for (const FVertex& Vertex : Data.Vertices)
		{
			if (!bHasBounds)
			{
				Min = Max = Vertex.Position;
				bHasBounds = true;
				continue;
			}
			Min = FVector(std::min(Min.X, Vertex.Position.X), std::min(Min.Y, Vertex.Position.Y), std::min(Min.Z, Vertex.Position.Z));
			Max = FVector(std::max(Max.X, Vertex.Position.X), std::max(Max.Y, Vertex.Position.Y), std::max(Max.Z, Vertex.Position.Z));
		}
	}
	return bHasBounds ? FBox{ Min, Max } : Super::CalcLocalBounds();	// 천이 없으면 기본(크기 0) 박스
}

void UClothComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
	UMaterial* Material = GetMaterial(0);
	if (!Proxy || !Material) return;

	for (int i = 0;i < ClothMeshDatas.Num();i++)
	{
		FClothMeshData& Data = ClothMeshDatas[i];
		if (!Data.VertexBuffer || !Data.IndexBuffer) continue;

		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Proxy = Proxy;
		Packet.VertexBuffer = Data.VertexBuffer.get();
		Packet.IndexBuffer = Data.IndexBuffer.get();
		Packet.Material = Material;
		Packet.IndexCount = Data.Indices.Num();

	}
}
