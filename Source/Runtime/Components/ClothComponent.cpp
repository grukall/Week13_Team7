#include "EnginePCH.h"
#include "ClothComponent.h"
#include "Physics/NvClothManager.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"
#include "UObject/UObjectIterator.h"
#include "Physics/BodySetup.h"
#include "Engine/ClothSceneProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"	// USkeletalBodySetup도 함께 들어온다
#include "UObject/Casts.h"
#include "Engine/World.h"
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h> 
#include <NvCloth/Cloth.h> 
#include <NvClothExt/ClothFabricCooker.h>
#include <foundation/PxQuat.h>		// NvCloth 헤더는 PxQuat를 앞선언만 한다 (setRotation·teleportToLocation 인자)
#include <algorithm>	
#include <cmath>		

namespace
{
	// NvCloth 로컬 공간(시뮬레이션 공간)의 월드 위치·회전. setTranslation / setRotation / teleportToLocation에 넣는다.
	// 입력은 GetSimToWorldMatrix(천의 R·T). 충돌체 변환(WorldToCloth)과 같은 행렬에서 뽑아야 중력 방향과 충돌 위치가 맞는다
	physx::PxVec3 ToClothPosition(const FMatrix& SimToWorld)
	{
		return physx::PxVec3(SimToWorld[3][0], SimToWorld[3][1], SimToWorld[3][2]);
	}

	// 엔진 쿼터니언은 PhysX와 같은 배치라 성분을 그대로 옮긴다 (PhysicsInterface.cpp ToPx와 같은 규칙)
	physx::PxQuat ToClothRotation(const FMatrix& SimToWorld)
	{
		const FQuat Q = SimToWorld.GetRotation();
		const physx::PxQuat Rotation(Q.X, Q.Y, Q.Z, Q.W);
		const float LengthSquared = Rotation.magnitudeSquared();
		return (std::isfinite(LengthSquared) && LengthSquared > 1.0e-12f) ? Rotation.getNormalized() : physx::PxQuat(physx::PxIdentity);
	}

	float ComputeGridMaxDistance(int32 row, int32 col, int32 Rows, int32 Cols, const FClothGridSettings& Grid, const FClothPinSettings& PinSetting)
	{
		if (Grid.Shape == EClothGridShape::Cape)
		{
			// 망토: 윗변(row 0)은 고정, 아래로 갈수록 반경이 커져 윗부분은 등에 붙고 밑단만 펄럭인다 (UE 천 페인팅의 MaxDistance 그라데이션)
			if (row == 0) return 0.0f;
			const float t = row / static_cast<float>(Rows - 1);
			return std::max(Grid.CapeFreeDistance, 0.0f) * std::pow(t, std::max(Grid.CapeFreeExponent, 0.1f));
		}

		const bool onRowFirst = PinSetting.bPinRowFirst && row == 0;
		const bool onRowLast  = PinSetting.bPinRowLast  && row == Rows - 1;
		const bool onColFirst = PinSetting.bPinColFirst && col == 0;
		const bool onColLast  = PinSetting.bPinColLast  && col == Cols - 1;

		const bool onChecked = onRowFirst || onRowLast || onColFirst || onColLast;	// 체크한 변 중 하나라도 위에 있으면

		bool pinned;
		if (PinSetting.bCornersOnly)
		{
			const bool isCorner = (row == 0 || row == Rows - 1) && (col == 0 || col == Cols - 1);
			pinned = onChecked && isCorner;		// 체크한 변의 양 끝 점만
		}
		else
		{
			pinned = onChecked;
		}

		return pinned ? 0.0f : std::max(PinSetting.FreeMaxDistance, 0.0f);	// 음수 반경은 NvCloth가 0(고정)으로 처리하므로 막는다
	}

	// 격자 한 점의 위치. 점 번호(r * Cols + c)·삼각형·UV·고정 규칙은 모양과 상관없이 같다
	FVector MakeGridPosition(int32 r, int32 c, const FClothGridSettings& Grid)
	{
		if (Grid.Shape == EClothGridShape::Cape)
		{
			// 망토: 세로(YZ) 평면의 사다리꼴. 줄마다 폭을 TopWidth → BottomWidth로 키워 아래가 퍼진다
			const float t = r / static_cast<float>(Grid.Rows - 1);			// 0 = 윗변, 1 = 밑단
			const float u = c / static_cast<float>(Grid.Cols - 1) - 0.5f;	// -0.5 = 왼쪽(-Y), 0.5 = 오른쪽(+Y)
			const float Width = Grid.CapeTopWidth + (Grid.CapeBottomWidth - Grid.CapeTopWidth) * t;
			// 가로 단면을 포물선으로 휜다: u = 0(가운데)에서 CurveDepth, u = ±0.5(양 끝)에서 0. 예) Depth 0.15 → 가운데가 15cm 튀어나온 반원통 느낌
			const float Curve = Grid.CapeCurveDepth * (1.0f - 4.0f * u * u);
			return FVector(Curve, u * Width, -t * Grid.CapeLength);
		}
		const float Spacing = Grid.Spacing;
		return FVector(c * Spacing, r * Spacing, 0.0f);	// 깃발: 원점 = 모서리, XY 평면
	}

	FClothBuildInput MakeDefaultGridInput(const FClothGridSettings& Grid, const FClothPinSettings& PinSettings)		// 에셋 등록 전 Default로 사용하는 천의 FClothBuildInput 만드는 함수
	{
		FClothBuildInput Input;
		const int32 Rows = Grid.Rows;
		const int32 Cols = Grid.Cols;
		const float Spacing = Grid.Spacing;

		if (Rows < 2 || Cols < 2 || Spacing <= 0.0f)
		{
			return Input;
		}

		const int32 NumPoints  = Rows * Cols;
		const int32 NumIndices = (Rows - 1) * (Cols - 1) * 6;
		Input.Positions.Reserve(static_cast<uint32>(NumPoints));
		Input.InvMasses.Reserve(static_cast<uint32>(NumPoints));
		Input.UVs.Reserve(static_cast<uint32>(NumPoints));
		Input.MaxDistances.Reserve(static_cast<uint32>(NumPoints));
		Input.Indices.Reserve(static_cast<uint32>(NumIndices));

		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				Input.Positions.Add(MakeGridPosition(r, c, Grid));
				Input.InvMasses.Add(1.0f);
				//Input.InvMasses.Add(r == 0 ? 0.0f : 1.0f);	// 첫 줄(r == 0)만 고정점. 모든 점에 하나씩 넣어야 Positions와 개수가 맞는다
				Input.UVs.Add(FVector2(c / static_cast<float>(Cols - 1), r / static_cast<float>(Rows - 1)));	// 격자 전체를 0~1로 펼친 UV
				Input.MaxDistances.Add(ComputeGridMaxDistance(r, c, Rows, Cols, Grid, PinSettings));	// 점마다 1개 (Positions와 같은 번호)
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

	constexpr int32 MaxClothCollisionShapes = 32;	// ClothImpl.h의 NV_CLOTH_ASSERT(newSize <= 32)

	FKBoxElem GetScaledBox(const FKBoxElem& Box, const FVector& Scale3D)
	{
		const FVector Size = Scale3D.GetAbs();
		FKBoxElem Scaled;
		Scaled.X = Box.X * Size.X;
		Scaled.Y = Box.Y * Size.Y;
		Scaled.Z = Box.Z * Size.Z;
		Scaled.Center = Box.Center * Scale3D;
		Scaled.Rotation = Box.Rotation;
		return Scaled;
	}

	// 박스 = 바깥을 향한 평면 6개 + 그 6개를 묶은 convex 1개. ScaledBox는 스케일이 이미 적용된 값이어야 한다
	bool AddBox(FClothCollisionShapes& Out, const FKBoxElem& ScaledBox, const FMatrix& RigidToCloth, float Pad)
	{
		if (Out.Planes.Num() + 6 > MaxClothCollisionShapes || Out.ConvexMasks.Num() + 1 > MaxClothCollisionShapes)
		{
			return false;
		}

		// X·Y·Z는 전체 길이 → 반축. 박스 자체 회전 → body 포즈(회전·이동) → 천 로컬 순서
		const FVector Center = RigidToCloth.TransformPosition(ScaledBox.Center);
		const FVector Axes[3] = {
			RigidToCloth.TransformVector(ScaledBox.Rotation.RotateVector(FVector(ScaledBox.X * 0.5f, 0.0f, 0.0f))),
			RigidToCloth.TransformVector(ScaledBox.Rotation.RotateVector(FVector(0.0f, ScaledBox.Y * 0.5f, 0.0f))),
			RigidToCloth.TransformVector(ScaledBox.Rotation.RotateVector(FVector(0.0f, 0.0f, ScaledBox.Z * 0.5f))),
		};

		const uint32 BasePlane = Out.Planes.Num();	// 이 박스의 첫 평면 번호 (예산 검사로 26 이하)
		for (const FVector& Axis : Axes)
		{
			for (const float Sign : { 1.0f, -1.0f })
			{
				const FVector Normal = (Axis * Sign).GetSafeNormal();			// 바깥 법선 (NvCloth는 정규화된 법선을 요구)
				const FVector OnPlane = Center + Axis * Sign + Normal * Pad;	// 면 위의 점을 Pad만큼 바깥으로
				Out.Planes.Add(FVector4(Normal.X, Normal.Y, Normal.Z, -FVector::Dot(Normal, OnPlane)));	// n·x + d < 0 이 박스 안쪽
			}
		}
		Out.ConvexMasks.Add(0x3Fu << BasePlane);	// 방금 넣은 평면 6개(비트 6개)가 한 덩어리
		return true;
	}

	// 구 1개. 비균등 스케일은 공용 GetFinalScaled 규칙(가장 작은 축 스케일)을 따른다 (NvCloth 구는 찌그러질 수 없음)
	bool AddSphere(FClothCollisionShapes& Out, const FKSphereElem& Sphere, const FVector& Scale3D, const FMatrix& RigidToCloth, float Pad)
	{
		if (Out.Spheres.Num() + 1 > MaxClothCollisionShapes)
		{
			return false;
		}
		const FKSphereElem Scaled = Sphere.GetFinalScaled(Scale3D);	// 반지름·중심에 스케일 적용 (PhysX body와 같은 함수)
		const FVector Center = RigidToCloth.TransformPosition(Scaled.Center);
		Out.Spheres.Add(FVector4(Center, Scaled.Radius + Pad));
		return true;
	}

	// 캡슐 = 양 끝 구 2개 + 두 구 번호를 잇는 캡슐 1개. Sphyl은 로컬 Z축 방향이고 Length는 반구를 뺀 원통 길이
	bool AddSphyl(FClothCollisionShapes& Out, const FKSphylElem& Sphyl, const FVector& Scale3D, const FMatrix& RigidToCloth, float Pad)
	{
		if (Out.Spheres.Num() + 2 > MaxClothCollisionShapes || Out.CapsuleIndices.Num() / 2 + 1 > MaxClothCollisionShapes)
		{
			return false;
		}
		const FKSphylElem Scaled = Sphyl.GetFinalScaled(Scale3D);	// 반지름 = max(X,Y) 스케일, 원통 길이 = Z 스케일 기준 (PhysX body와 같은 함수)
		const FVector HalfSegment = Scaled.Rotation.RotateVector(FVector(0.0f, 0.0f, Scaled.Length * 0.5f));
		const FVector End0 = RigidToCloth.TransformPosition(Scaled.Center + HalfSegment);
		const FVector End1 = RigidToCloth.TransformPosition(Scaled.Center - HalfSegment);
		const float Radius = Scaled.Radius + Pad;

		const uint32 FirstSphere = Out.Spheres.Num();
		Out.Spheres.Add(FVector4(End0, Radius));
		Out.Spheres.Add(FVector4(End1, Radius));
		Out.CapsuleIndices.Add(FirstSphere);
		Out.CapsuleIndices.Add(FirstSphere + 1);
		return true;
	}

	// Convex는 임시로 정점 AABB를 박스로 넣는다. PxConvexMesh의 polygon 평면을 쓸 수도 있지만 천 충돌 평면은 32개가 상한이다
	bool AddConvexAsBox(FClothCollisionShapes& Out, const FKConvexElem& Convex, const FVector& Scale3D, const FMatrix& RigidToCloth, float Pad)
	{
		if (Convex.VertexData.IsEmpty())
		{
			return false;
		}
		// PhysX는 convex 정점에 축별 스케일(부호 포함, 음수는 NegX 메시·회전으로 처리)을 적용한다. 같은 결과가 되도록 스케일한 정점으로 AABB를 만든다
		FVector Min = Convex.VertexData[0] * Scale3D;
		FVector Max = Min;
		for (const FVector& Vertex : Convex.VertexData)		// VertexData는 바디 로컬
		{
			const FVector V = Vertex * Scale3D;
			Min = FVector(std::min(Min.X, V.X), std::min(Min.Y, V.Y), std::min(Min.Z, V.Z));
			Max = FVector(std::max(Max.X, V.X), std::max(Max.Y, V.Y), std::max(Max.Z, V.Z));
		}

		FKBoxElem ScaledBox;	// 이미 스케일된 정점으로 만들었으므로 GetScaledBox를 다시 거치지 않는다
		ScaledBox.Center = (Min + Max) * 0.5f;
		const FVector Size = Max - Min;
		ScaledBox.X = Size.X;
		ScaledBox.Y = Size.Y;
		ScaledBox.Z = Size.Z;
		return AddBox(Out, ScaledBox, RigidToCloth, Pad);
	}

	// 바디 하나의 도형 묶음을 모두 넣는다. Scale3D는 도형에 먼저 곱하고, RigidToCloth(스케일 없는 바디 포즈 → 천 로컬)로 옮긴다
	bool AddAggGeom(FClothCollisionShapes& Out, const FKAggregateGeom& Geom, const FVector& Scale3D, const FMatrix& RigidToCloth, float Pad)
	{
		bool bAllAdded = true;
		for (const FKBoxElem& Box : Geom.BoxElems)          bAllAdded &= AddBox(Out, GetScaledBox(Box, Scale3D), RigidToCloth, Pad);	// 평면 6개 + convex 1개
		for (const FKSphereElem& Sphere : Geom.SphereElems) bAllAdded &= AddSphere(Out, Sphere, Scale3D, RigidToCloth, Pad);			// 구 1개
		for (const FKSphylElem& Sphyl : Geom.SphylElems)    bAllAdded &= AddSphyl(Out, Sphyl, Scale3D, RigidToCloth, Pad);			// 구 2개 + 캡슐 1개
		for (const FKConvexElem& Convex : Geom.ConvexElems) bAllAdded &= AddConvexAsBox(Out, Convex, Scale3D, RigidToCloth, Pad);	// 정점 AABB를 박스로 (임시)
		return bAllAdded;
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

	if (!CreateSolver())	// 천 계산할 Solver 세팅 (컴포넌트의 모든 천이 공유). 등록할 때 전역 CPU/CUDA 설정대로 만든다
	{
		return;
	}

	BuildDefaultCloth();
	OnTransformDirty();		// Proxy가 천 생성 전에 만들어졌을 수 있으므로 초기 바운드를 반영

}

void UClothComponent::OnUnregister()
{
	DestroyAllCloths();
	DestroySolver();	// 천이 모두 빠진 뒤에 Solver 삭제
	Super::OnUnregister();
}

bool UClothComponent::CreateSolver()
{
	FNvClothManager& Manager = FNvClothManager::Get();
	BuiltBackendSerial = Manager.GetBackendSerial();	// 실패·대체 여부와 상관없이 이번 설정으로 시도했음을 기록 (매 프레임 재시도 방지)
	const bool bUseCUDA = Manager.GetUseCUDA();
	if (bUseCUDA && !Manager.IsCUDAAvailable())
	{
		HTR_LOG(Warning, "[NvCloth] CUDA requested but unavailable (CPU-only build or no NVIDIA driver/device). Using CPU solver.");
	}

	SimFactory = Manager.GetFactory(bUseCUDA);	// CUDA가 없으면 CPU Factory
	if (SimFactory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Factory is null (NvClothManager not initialized)");
		return false;
	}

	Solver = SimFactory->createSolver();
	if (Solver == nullptr && Manager.IsCUDAFactory(SimFactory))
	{
		// CUDA 솔버 생성 실패 (커널 로드 실패 등) → CPU로 대체
		HTR_LOG(Warning, "[NvCloth] CUDA createSolver failed. Falling back to CPU solver.");
		SimFactory = Manager.GetFactory(false);
		Solver = SimFactory ? SimFactory->createSolver() : nullptr;
	}
	if (Solver == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] createSolver failed");
		SimFactory = nullptr;
		return false;
	}

	HTR_LOG(Info, "[NvCloth] {} solver: {}", GetName(), Manager.IsCUDAFactory(SimFactory) ? "CUDA" : "CPU");
	return true;
}

void UClothComponent::DestroySolver()
{
	if (Solver != nullptr)
	{
		NV_CLOTH_DELETE(Solver);
		Solver = nullptr;
	}
	SimFactory = nullptr;
}

FVector UClothComponent::GetSimScale() const
{
	constexpr float MinScale = 1.0e-4f;	// 렌더 직전 ÷S에서 0으로 나누지 않도록
	const FVector Scale = GetWorldScale3D().GetAbs();
	return FVector(std::max(Scale.X, MinScale), std::max(Scale.Y, MinScale), std::max(Scale.Z, MinScale));
}

FMatrix UClothComponent::GetSimToWorldMatrix() const
{
	// 엔진 월드 행렬은 "회전 행렬의 i행 × Scale_i" + 4행 이동 (Transform.cpp GetLocalMatrix).
	// 위 3행을 정규화하면 Proxy가 쓰는 행렬과 같은 R·T가 나온다 (회전 Rotator 왕복 오차 없음)
	FMatrix M = GetWorldMatrix();
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Length = std::sqrt(M[Row][0] * M[Row][0] + M[Row][1] * M[Row][1] + M[Row][2] * M[Row][2]);
		if (Length > 1.0e-8f)
		{
			M[Row][0] /= Length;
			M[Row][1] /= Length;
			M[Row][2] /= Length;
		}
	}
	return M;
}

bool UClothComponent::BuildDefaultCloth()
{
	// 에셋 등록 전에는 기본 격자 천 하나를 추가한다. 에셋이 생기면 그 입력으로 CreateCloth를 호출한다.
	if (!CreateCloth(MakeDefaultGridInput(GridSettings, PinSettings)))	// 고정 설정으로 점마다 반경(MaxDistances)을 채운다
	{
		return false;
	}
	// 실행 중 고정 설정 변경(RefreshPinsFromSettings)에서 같은 점 번호로 다시 계산하기 위해 기억
	BuiltGridRows = GridSettings.Rows;
	BuiltGridCols = GridSettings.Cols;
	BuiltGridSettings = GridSettings;
	DefaultGridClothIndex = ClothInstances.Num() - 1;	// 방금 Add된 천
	return true;
}

void UClothComponent::DestroyAllCloths()
{
	for (FClothInstance& Instance : ClothInstances)
	{
		DestroyCloth(Instance);
	}
	ClothInstances.Reset();
	ClothMeshDatas.Reset();		// ClothInstances와 같은 번호로 짝을 이루므로 함께 비운다
	BuiltGridRows = 0;
	BuiltGridCols = 0;
	DefaultGridClothIndex = -1;
}

void UClothComponent::RebuildCloth(bool bRecreateSolver, bool bRegenerateGrid)
{
	// 스케일은 Fabric(스프링 원래 길이)에 구워지므로, 바뀌면 천을 새로 만들어야 한다. 천은 처음 모양으로 리셋된다
	// CPU/CUDA를 바꿀 때는 Factory가 바뀌므로 Solver까지 새 Factory로 만든다 (다른 Factory의 Cloth를 Solver에 넣을 수 없다)
	// 기본 격자뿐 아니라 컴포넌트가 가진 모든 천을 각자의 원본 입력으로 다시 만든다

	const int32 OldGridIndex = DefaultGridClothIndex;
	int32 GridRows = BuiltGridRows;
	int32 GridCols = BuiltGridCols;
	FClothGridSettings GridBuiltWith = BuiltGridSettings;

	// 1. 지우기 전에 천마다 원본 입력을 모은다. 실행 중 바뀐 고정 반경(RefreshPinsFromSettings)은 Instance 쪽이 최신이므로 덮어쓴다
	TArray<FClothBuildInput> Inputs;
	Inputs.Reserve(ClothInstances.Num());
	for (int32 i = 0; i < ClothInstances.Num(); ++i)
	{
		const FClothInstance& Instance = ClothInstances[i];
		if (bRegenerateGrid && i == OldGridIndex)
		{
			// 기본 격자 천은 현재 GridSettings·PinSettings로 새 입력을 만든다 (점 개수가 바뀌므로 이전 반경은 쓰지 않는다)
			Inputs.Add(MakeDefaultGridInput(GridSettings, PinSettings));
			GridRows = GridSettings.Rows;
			GridCols = GridSettings.Cols;
			GridBuiltWith = GridSettings;
			continue;
		}
		FClothBuildInput Input = Instance.SourceInput;
		if (Instance.MaxDistances.Num() == Input.Positions.Num())
		{
			Input.MaxDistances = Instance.MaxDistances;
		}
		Inputs.Add(std::move(Input));
	}

	// 2. 전부 지운다. 이전 Factory의 Cloth·Fabric을 먼저 해제한 뒤 Solver를 바꾼다
	DestroyAllCloths();
	if (bRecreateSolver)
	{
		DestroySolver();
		if (!CreateSolver())
		{
			OnTransformDirty();
			return;		// Solver가 없으면 천을 만들 수 없다 (CreateSolver가 로그를 남김)
		}
	}

	// 3. 같은 순서로 다시 만든다 (CreateCloth가 현재 스케일을 굽고, 현재 SimFactory를 쓴다)
	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		if (!CreateCloth(Inputs[i]))
		{
			HTR_LOG(Error, "[NvCloth] Rebuild failed for cloth {}", i);
			continue;
		}
		if (i == OldGridIndex)	// 기본 격자 천이면 번호·격자 정보를 다시 기록 (실패한 천이 있으면 번호가 당겨질 수 있으므로 새 번호로)
		{
			DefaultGridClothIndex = ClothInstances.Num() - 1;
			BuiltGridRows = GridRows;
			BuiltGridCols = GridCols;
			BuiltGridSettings = GridBuiltWith;
		}
	}
	OnTransformDirty();		// 새 정점으로 바운드 갱신
}

// 입력 하나로 Fabric Cook → Cloth 생성·설정 → Solver에 추가 → ClothInstances에 보관. 실패하면 false
// RawInput.Positions는 렌더 로컬(스케일 1 기준). 여기서 천의 스케일을 구워 시뮬레이션 공간 좌표로 바꾼다
bool UClothComponent::CreateCloth(const FClothBuildInput& RawInput)
{
	if (Solver == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] CreateCloth called without Solver");
		return false;
	}
	nv::cloth::Factory* Factory = SimFactory;	// Solver를 만든 Factory와 같아야 한다 (Fabric·Cloth를 다른 Factory로 만들면 Solver에 넣을 수 없음)
	if (Factory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] CreateCloth called without SimFactory");
		return false;
	}
	if (RawInput.Positions.Num() == 0 || RawInput.InvMasses.Num() != RawInput.Positions.Num() || RawInput.UVs.Num() != RawInput.Positions.Num() || RawInput.Indices.Num() % 3 != 0 || RawInput.MaxDistances.Num() != RawInput.Positions.Num())
	{
		HTR_LOG(Error, "[NvCloth] Cloth build input is invalid");
		return false;
	}

	// 스케일 굽기: 점 위치 × S. 아래 코드는 모두 이 Input을 쓰므로
	// Fabric 원래 길이·시작 위치·RestPositions·MotionTargets·렌더 초기값이 한 번에 실제 크기 기준이 된다.
	// UV·Indices·InvMasses·MaxDistances(월드 길이)는 크기와 무관해 그대로 둔다.
	// MakeClothMeshDesc가 Positions 포인터만 저장하므로 Input은 Fabric 굽기가 끝날 때까지 살아 있어야 한다 (함수 지역 변수)
	FClothBuildInput Input = RawInput;
	BuiltScale = GetSimScale();
	for (FVector& Position : Input.Positions)
	{
		Position = Position * BuiltScale;
	}

	nv::cloth::ClothMeshDesc ClothDesc = MakeClothMeshDesc(Input);	// Fabric Cook용 Desc

	physx::PxVec3 gravity(0.0f, 0.0f, -1.0f);  // 기본 중력 세팅
	nv::cloth::Vector<int32_t>::Type phaseTypeInfo;	// phaseType 배열을 저장, 해당 배열은 i번째 phase의 종류를 담고 있음 (선 종류 / vertical 등등)

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
		if (phaseTypeInfo[i] == nv::cloth::ClothFabricPhaseType::eVERTICAL || phaseTypeInfo[i] == nv::cloth::ClothFabricPhaseType::eHORIZONTAL)
		{
			confi.mStiffness = 1.0f;
		}
		if (phaseTypeInfo[i] == nv::cloth::ClothFabricPhaseType::eSHEARING)
		{
			confi.mStiffness = 0.6f;
		}
		if (phaseTypeInfo[i] == nv::cloth::ClothFabricPhaseType::eBENDING)
		{
			confi.mStiffness = 0.3f;
		}
		Configs.Add(confi);	//  Fabric의 Phase 만큼 PhaseConfig 세팅
	}
	Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(Configs.GetData(), Configs.GetData() + Configs.Num()));	// PhaseConfig 세팅
	Cloth->setDragCoefficient(0.05f);   // 공기 저항
	Cloth->setLiftCoefficient(0.2f);   // 양력 (천이 들썩이는 힘)

	Cloth->setRestPositions(ParticleRange);

	// 새 천의 로컬 공간은 (원점, 회전 없음)에 있다고 NvCloth가 알고 있다. 그대로 두면 다음 Tick의 setTranslation이
	// "원점에서 현재 위치까지 한 프레임에 이동"으로 계산되어 큰 관성이 생기므로, 처음부터 현재 위치에 있던 것으로 맞춘다
	const FMatrix SimToWorld = GetSimToWorldMatrix();
	Cloth->teleportToLocation(ToClothPosition(SimToWorld), ToClothRotation(SimToWorld));

	FClothMeshData ClothData;	// Cloth Rendering용 데이터 생성 (앞면·뒷면 두 겹, 정점 2N개)
	ClothData.InitTwoSided(Input.Positions, Input.UVs, Input.Indices);
	ClothData.FinalizeTwoSided(RenderThickness);		// 시뮬레이션 공간에서 법선·두께 (월드 단위)
	ClothData.ConvertSimToRenderSpace(BuiltScale);		// 첫 업로드 전에 렌더 로컬로 (빠지면 첫 프레임만 S배로 보임)

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

	FClothInstance Instance{ Fabric, Cloth, Input.Indices };
	Instance.MotionTargets = Input.Positions;			// 지켜야 하는 초기 위치, SkeletalMesh의 경우 Bone 정점으로 Tick마다 업데이트
	Instance.MaxDistances = Input.MaxDistances;			// 점의 최대 반경
	Instance.SourceInput = RawInput;					// 스케일 굽기 전 원본. RebuildCloth가 새 스케일로 다시 구울 때 쓴다
	ClothInstances.Add(Instance);
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

	// CPU/CUDA 전환은 simulate 밖(Tick 맨 앞)에서만 한다. begin~endSimulation 사이에 Solver·Cloth를 지우면 안 된다
	if (BuiltBackendSerial != FNvClothManager::Get().GetBackendSerial())	// Settings 패널에서 CPU/CUDA가 바뀌었다
	{
		const bool bHadCloth = !ClothInstances.IsEmpty();
		RebuildCloth(true);
		if (!bHadCloth && Solver != nullptr && ClothInstances.IsEmpty())
		{
			BuildDefaultCloth();	// 등록 때 Solver 생성에 실패해 천이 없었던 경우, 새 Solver로 기본 격자를 만든다
			OnTransformDirty();
		}
	}

	if (Solver == nullptr) return;

	// 격자 설정 변경도 같은 이유로 simulate 밖에서 다시 만든다
	if (bGridDirty)
	{
		RegenerateDefaultGrid();
	}

	if (!(DeltaTime > 0.0f) || !std::isfinite(DeltaTime)) return;
	constexpr float MaxSimDeltaTime = 1.0f / 30.0f;
	float dt = std::min(DeltaTime, MaxSimDeltaTime);

	if (!ClothInstances.IsEmpty())
	{
		const FVector CurrentScale = GetSimScale();
		constexpr float ScaleTolerance = 1.0e-4f;
		if (std::abs(CurrentScale.X - BuiltScale.X) > ScaleTolerance ||
			std::abs(CurrentScale.Y - BuiltScale.Y) > ScaleTolerance ||
			std::abs(CurrentScale.Z - BuiltScale.Z) > ScaleTolerance)
		{
			RebuildCloth();
			if (ClothInstances.IsEmpty()) return;
		}
	}

	physx::PxVec3 Wind(0.0f);
	if (SimSettings.bEnableWind)
	{
		const float WindStrength = SimSettings.WindStrength;
		WindTime += DeltaTime;
		const float Gust = 1.0f + 0.5f * std::sin(WindTime * 2.0f) + 0.25f * std::sin(WindTime * 5.3f);
		Wind = physx::PxVec3(
			0.3f * WindStrength * std::sin(WindTime * 1.3f),
			WindStrength * Gust,
			0.2f * WindStrength * std::sin(WindTime * 3.7f));
	}

	// 월드 → 시뮬레이션 공간: 천의 R·T만 되돌리고 S⁻¹은 적용하지 않는다.
	// 천 모양에 이미 S가 구워져 있으므로 충돌체 반지름·Pad를 월드 값 그대로 쓸 수 있다
	const FMatrix SimToWorld = GetSimToWorldMatrix();
	const FMatrix WorldToCloth = SimToWorld.Inverse();
	GatherCollisionShape(CollisionShapes, WorldToCloth);

	// 천 로컬 공간이 월드에서 어디 있는지 NvCloth에 알린다 (관성, 월드 기준 중력·바람). 파티클은 계속 로컬 좌표다
	const physx::PxVec3 FramePosition = ToClothPosition(SimToWorld);
	const physx::PxQuat FrameRotation = ToClothRotation(SimToWorld);
	const bool bLongFrame = DeltaTime > MaxSimDeltaTime;	// 실제 시간이 dt 상한보다 길면 속도(이동량 / dt)가 부풀어 천이 튄다

	const physx::PxVec3 LinearInertia(std::clamp(SimSettings.LinearInertia, 0.0f, 1.0f));
	const physx::PxVec3 AngularInertia(std::clamp(SimSettings.AngularInertia, 0.0f, 1.0f));
	const physx::PxVec3 CentrifugalInertia(std::clamp(SimSettings.CentrifugalInertia, 0.0f, 1.0f));

	for (FClothInstance& Instance : ClothInstances)
	{
		if (Instance.Cloth != nullptr)
		{
			// 지난번에 넣은 위치는 NvCloth가 갖고 있다 (getTranslation). 한 프레임에 너무 멀리 갔으면 기즈모 점프·리스폰으로 본다
			const float MoveDistance = (FramePosition - Instance.Cloth->getTranslation()).magnitude();
			if (bLongFrame || MoveDistance > std::max(SimSettings.TeleportDistance, 0.0f))
			{
				// 관성 없이 위치만 옮기고, 이번 프레임은 지난 속도를 유지한다 (속도가 0으로 끊기면 감속 관성으로 또 튄다)
				Instance.Cloth->teleportToLocation(FramePosition, FrameRotation);
				Instance.Cloth->ignoreVelocityDiscontinuity();
			}
			else
			{
				Instance.Cloth->setTranslation(FramePosition);	// NvCloth가 지난 값과 비교해 관성을 만들고
				Instance.Cloth->setRotation(FrameRotation);		// 월드 기준 중력·바람을 로컬 방향으로 바꾼다
			}
			Instance.Cloth->setLinearInertia(LinearInertia);
			Instance.Cloth->setAngularInertia(AngularInertia);
			Instance.Cloth->setCentrifugalInertia(CentrifugalInertia);
			Instance.Cloth->setSolverFrequency(std::max(SimSettings.SolverFrequency, 1.0f));	// 값이 같으면 NvCloth가 바로 돌아간다 (ClothImpl.h setSolverFrequency)

			Instance.Cloth->setWindVelocity(Wind);		// 월드 기준 (NvCloth가 로컬 방향으로 바꾼다)
			Instance.Cloth->setGravity(SimSettings.bEnableGravity ? physx::PxVec3(0.0f, 0.0f, -9.81f) : physx::PxVec3(0.0f));	// 월드 기준 (회전을 알려 줬으므로 천이 기울어도 월드 아래로 걸린다)
			Instance.Cloth->setSelfCollisionDistance(SelfCollision.bEnabled ? std::max(SelfCollision.Distance, 0.0f) : 0.0f);
			Instance.Cloth->setSelfCollisionStiffness(std::clamp(SelfCollision.Stiffness, 0.0f, 1.0f));
			Instance.Cloth->setFriction(std::max(SimSettings.Friction, 0.0f));
			ApplyClothCollision(*Instance.Cloth, CollisionShapes);	// 수집한 전체 충돌체를 Cloth에 등록, 해당 Cloth와 충돌체가 상호작용하도록 함
			ApplyMotionConstraints(Instance);	// 고정점·반경이 바뀌었으면(dirty) simulate 전에 NvCloth에 반영
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
		Data.FinalizeTwoSided(RenderThickness);		// 뒷면 복사 → 법선 → 두께 오프셋 → 탄젠트 (시뮬레이션 공간, 월드 단위)
		Data.ConvertSimToRenderSpace(BuiltScale);	// GPU가 S·R·T를 곱하므로 ÷S로 상쇄 (앞면 위치는 다음 Tick에 파티클로 다시 덮어씀)

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

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	return new FClothSceneProxy(this);	// 그림자 패스가 천을 Caster로 받도록 (HasDynamicMeshElements = true)
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

void UClothComponent::GatherCollisionShape(FClothCollisionShapes& CollisionShape, FMatrix WorldToCloth)
{
	CollisionShape.Reset();		// 매 프레임 새로 모은다. 빼먹으면 계속 쌓여 32개 상한 assert에 걸린다
	const float Pad = RenderThickness * 0.5f + 0.01f;	// 렌더 두께(뒷면)가 충돌체에 파고들지 않게 충돌체를 키우는 여유

	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* PrimComp = *It;
		if (PrimComp == this || !PrimComp->IsRegistered() || PrimComp->GetWorld() != GetWorld()) continue;

		const FBodyInstance* Body = PrimComp->GetBodyInstance();
		if (Body->CollisionEnabled == ECollisionEnabled::NoCollision) continue;
		if (Body->CollisionResponses.Cloth != ECollisionResponse::ECR_Block) continue;

		// SkeletalMesh일 경우
		const USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(PrimComp);
		if (SkelComp && SkelComp->bUseArticulatedPhysics)
		{
			bool bAllAdded = true;
			SkelComp->ForEachBodyShape([&](const USkeletalBodySetup& BodySetup, const FTransform& Pose, const FVector& Scale)
			{
				if (BodySetup.CollisionEnabled == ECollisionEnabled::NoCollision) return;	// Physics Asset 에디터에서 끈 바디
				const FMatrix RigidToCloth = Pose.GetLocalMatrix() * WorldToCloth;		// 바디 로컬(스케일 1) → 천 로컬
				bAllAdded &= AddAggGeom(CollisionShape, BodySetup.AggGeom, Scale, RigidToCloth, Pad);
			});
			if (!bAllAdded)
			{
				HTR_LOG(Warning, "[NvCloth] Cloth collision budget exceeded or empty convex; some bodies of {} were skipped", PrimComp->GetName());
			}
			continue;
		}

		// StaticMesh일경우
		const UBodySetup* Setup = PrimComp->GetBodySetup();		// 모양 없는 Primitive(스켈레탈 등)는 nullptr
		if (Setup == nullptr || Setup->AggGeom.IsEmpty()) continue;

		const FVector Scale3D = PrimComp->GetWorldScale3D();
		const FTransform BodyPose(PrimComp->GetWorldRotation(), PrimComp->GetWorldLocation());	// 스케일 1
		const FMatrix RigidToCloth = BodyPose.GetLocalMatrix() * WorldToCloth;	// Body 로컬→ Cloth 로컬 변환 행렬
		const bool bAllAdded = AddAggGeom(CollisionShape, Setup->AggGeom, Scale3D, RigidToCloth, Pad);
		if (!bAllAdded)
		{
			HTR_LOG(Warning, "[NvCloth] Cloth collision budget exceeded or empty convex; some shapes of {} were skipped", PrimComp->GetName());
		}
	}
}

void UClothComponent::ApplyClothCollision(nv::cloth::Cloth& Cloth, const FClothCollisionShapes& CollisionShape)
{
	TArray<physx::PxVec4> PxSpheres, PxPlanes;
	PxSpheres.Reserve(CollisionShape.Spheres.Num());
	PxPlanes.Reserve(CollisionShape.Planes.Num());

	for (int i = 0;i < CollisionShape.Spheres.Num();i++)
	{
		PxSpheres.Add(physx::PxVec4(CollisionShape.Spheres[i].X, CollisionShape.Spheres[i].Y, CollisionShape.Spheres[i].Z, CollisionShape.Spheres[i].W));
	}
	for (int i = 0;i < CollisionShape.Planes.Num();i++)
	{
		PxPlanes.Add(physx::PxVec4(CollisionShape.Planes[i].X, CollisionShape.Planes[i].Y, CollisionShape.Planes[i].Z, CollisionShape.Planes[i].W));
	}
	const TArray<uint32>& Capsules = CollisionShape.CapsuleIndices;
	const TArray<uint32>& ConvexMasks = CollisionShape.ConvexMasks;

	// 참조되는 쪽을 먼저: 구 → 캡슐, 평면 → convex
	Cloth.setSpheres(nv::cloth::Range<const physx::PxVec4>(PxSpheres.GetData(), PxSpheres.GetData() + PxSpheres.Num()), 0, Cloth.getNumSpheres());
	Cloth.setCapsules(nv::cloth::Range<const uint32>(Capsules.GetData(), Capsules.GetData() + Capsules.Num()), 0, Cloth.getNumCapsules());
	Cloth.setPlanes(nv::cloth::Range<const physx::PxVec4>(PxPlanes.GetData(), PxPlanes.GetData() + PxPlanes.Num()), 0, Cloth.getNumPlanes());
	Cloth.setConvexes(nv::cloth::Range<const uint32>(ConvexMasks.GetData(), ConvexMasks.GetData() + ConvexMasks.Num()), 0, Cloth.getNumConvexes());
}

void UClothComponent::ApplyMotionConstraints(FClothInstance& ClothInstance)
{
	if (ClothInstance.Cloth == nullptr || !ClothInstance.bMotionDirty) return;

	nv::cloth::Cloth& Cloth = *ClothInstance.Cloth;
	const uint32 NumParticles = Cloth.getNumParticles();

	if (ClothInstance.MotionTargets.Num() != static_cast<int32>(NumParticles) || ClothInstance.MaxDistances.Num() != static_cast<int32>(NumParticles))
	{
		HTR_LOG(Error, "[NvCloth] Motion constraint data count does not match particle count");
		ClothInstance.bMotionDirty = false;
		return;
	}

	nv::cloth::Range<physx::PxVec4> Constraints = Cloth.getMotionConstraints();		
	for (uint32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
	{
		const FVector& Target = ClothInstance.MotionTargets[ParticleIndex];
		const float MaxDist = std::max(ClothInstance.MaxDistances[ParticleIndex], 0.0f);
		Constraints[ParticleIndex] = physx::PxVec4(Target.X, Target.Y, Target.Z, MaxDist);		// 각 점의 Constraint에 MotionTarget 점을 기록
	}

	ClothInstance.bMotionDirty = false;
}

void UClothComponent::RefreshPinsFromSettings()
{
	if (DefaultGridClothIndex < 0 || DefaultGridClothIndex >= ClothInstances.Num()) return;	// 격자 천이 없음 (등록 전, 생성 실패)
	FClothInstance& Instance = ClothInstances[DefaultGridClothIndex];

	// 만들 때와 점 개수가 같을 때만 다시 계산한다. 다르면 점 번호가 어긋나 엉뚱한 점이 고정된다
	if (Instance.MaxDistances.Num() != BuiltGridRows * BuiltGridCols)
	{
		HTR_LOG(Warning, "[NvCloth] Pin refresh skipped: grid size does not match the built cloth");
		return;
	}

	// 격자 크기·점 위치는 그대로, 고정 설정만 바뀐 값으로 점마다 반경을 덮어쓴다 (처음 만들 때와 같은 함수)
	for (int32 r = 0; r < BuiltGridRows; ++r)
	{
		for (int32 c = 0; c < BuiltGridCols; ++c)
		{
			Instance.MaxDistances[r * BuiltGridCols + c] = ComputeGridMaxDistance(r, c, BuiltGridRows, BuiltGridCols, BuiltGridSettings, PinSettings);
		}
	}

	Instance.bMotionDirty = true;	// 다음 Tick의 ApplyMotionConstraints가 simulate 전에 NvCloth에 쓴다
}

void UClothComponent::SetPinSettings(const FClothPinSettings& NewSettings)
{
	PinSettings = NewSettings;
	RefreshPinsFromSettings();
}

void UClothComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);	// 부모(Primitive·Scene 등)의 처리를 먼저

	// SimSettings·SelfCollision·RenderThickness는 매 Tick 읽으므로 따로 할 일이 없다
	if (Property.Name == "PinSettings")
	{
		RefreshPinsFromSettings();	// 반경만 다시 계산 (천은 리셋되지 않음)
	}
	else if (Property.Name == "GridSettings")
	{
		bGridDirty = true;			// 점 개수가 바뀌므로 다음 Tick 시작에서 격자 천을 다시 만든다
		// 에디터 월드는 천 Tick을 돌리지 않으므로(UWorld::EditorWorldTick) 바로 다시 만들어야 모양·위치를 보며 배치할 수 있다
		const UWorld* World = GetWorld();
		if (World && !World->IsGameWorld())
		{
			RegenerateDefaultGrid();
		}
	}
}

void UClothComponent::RegenerateDefaultGrid()
{
	bGridDirty = false;
	if (Solver == nullptr) return;
	RebuildCloth(false, true);
	if (DefaultGridClothIndex < 0 && BuildDefaultCloth())	// 격자 천이 없었거나 만들기에 실패했던 경우
	{
		OnTransformDirty();
	}
}


bool UClothComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	bool bHit = false;
	for (int k = 0;k < ClothMeshDatas.Num();k++)
	{
		const TArray<FVertex>* Vertices = &ClothMeshDatas[k].Vertices;
		const TArray<uint32>& Indices = ClothMeshDatas[k].Indices;
		for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
		{
			float T = FLT_MAX;
			if (RayIntersectsTriangle(LocalRay, (*Vertices)[Indices[I]].Position,(*Vertices)[Indices[I + 1]].Position, (*Vertices)[Indices[I + 2]].Position, T) && T < OutT)
			{
				OutT = T;
				bHit = true;
			}
		}
	}
	return bHit;
}