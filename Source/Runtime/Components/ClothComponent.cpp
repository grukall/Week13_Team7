#include "EnginePCH.h"
#include "ClothComponent.h"
#include "Physics/NvClothManager.h"
#include <NvCloth/Factory.h> 
#include <NvCloth/Solver.h> 
#include <NvCloth/Cloth.h> 
#include <NvClothExt/ClothFabricCooker.h>

namespace
{
	// 천 한 장을 만드는 데 필요한 원본 정보. 격자·메시 에셋 등 원본이 무엇이든 이 형태로 채움
	struct FClothBuildInput
	{
		TArray<FVector> Positions;	// 점 위치 (컴포넌트 로컬 좌표)
		TArray<uint32>  Indices;	// 삼각형 점 번호, 3개씩 (파티클 번호 기준)
		TArray<float>   InvMasses;	// 점 역질량. 0 = 고정점, Positions와 개수가 같아야 한다
	};

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
		Input.Indices.Reserve(static_cast<uint32>(NumIndices));

		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				Input.Positions.Add(FVector(c * Spacing, 0.0f, -r * Spacing));
				Input.InvMasses.Add(r == 0 ? 0.0f : 1.0f);
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
}

UClothComponent::~UClothComponent()
{
}

void UClothComponent::OnRegister()
{
	Super::OnRegister();
	FClothBuildInput DefaultClothInput = MakeDefaultGridInput(3, 3, 2.0f);
	if (DefaultClothInput.Positions.Num() == 0)
	{
		HTR_LOG(Error, "[NvCloth] Cloth build input is empty");
		return;
	}
	nv::cloth::ClothMeshDesc DefaultGridDesc = MakeClothMeshDesc(DefaultClothInput);	// Fabric Cook용 Desc
	nv::cloth::Factory* Factory = FNvClothManager::Get().GetFactory();  // NvCloth용 관련 객체들 생성하기 위한 Factory
	if (Factory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Factory is null (NvClothManager not initialized)");
		return;
	}
	physx::PxVec3 gravity(0.0f, 0.0f, -1.0f);  // 기본 중력 세팅
	nv::cloth::Vector<int32_t>::Type phaseTypeInfo;	// phaseType 배열을 저장, 해당 배열은 i번째 phase의 종류를 담고 있음 (선 종류 / vertical 등등)ㄱ

	TArray<physx::PxVec4> Particles;
	Particles.Reserve(DefaultClothInput.Positions.Num());
	for (int i = 0; i < DefaultClothInput.Positions.Num();i++)
	{
		FVector P = DefaultClothInput.Positions[i];
		Particles.Add(physx::PxVec4(P.X, P.Y, P.Z, DefaultClothInput.InvMasses[i]));		// 정점 xyz + 역질량 좌표 Particles 배열에 추가
	}
	nv::cloth::Range<const physx::PxVec4> ParticleRange(Particles.GetData(), Particles.GetData() + Particles.Num());		// Particle 배열 시작과 끝 지점 Range 구조체로 정의 (시작, 끝 지점 포인터 존재)
	
	Solver = Factory->createSolver();	// 천 계산할 Solver 세팅
	if (Solver == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] createSolver failed");
		return;
	}
	nv::cloth::Fabric* Fabric = NvClothCookFabricFromMesh(Factory, DefaultGridDesc, gravity, &phaseTypeInfo);
	if (Fabric == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Fabric cooking failed");
		return;
	}
	nv::cloth::Cloth* Cloth = Factory->createCloth(ParticleRange, *Fabric);
	if (Cloth == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] createCloth failed");
		Fabric->decRefCount();		// 굽기에서 받은 참조(1)를 반납 → Fabric 삭제
		return;
	}
	FClothInstance DefaultCloth = { Fabric, Cloth, DefaultClothInput.Indices };
	ClothInstances.Add(DefaultCloth);

	int32 PhaseNum = Fabric->getNumPhases();
	TArray<nv::cloth::PhaseConfig> Configs; 
	Configs.Reserve(PhaseNum);
	for (int i = 0;i < PhaseNum;i++)
	{
		Configs.Add(nv::cloth::PhaseConfig(i));			//  Fabric의 Phase 만큼 PhaseConfig 세팅
	}
	Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(Configs.GetData(), Configs.GetData() + Configs.Num()));	// PhaseConfig 세팅
	Cloth->setGravity(physx::PxVec3(0, 0, -9.81));	// 중력 세팅
	Solver->addCloth(Cloth);	// Solver에 Default 옷 추가
}

void UClothComponent::OnUnregister()
{
	// TODO
	Super::OnUnregister();
}

void UClothComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	// TODO
}

