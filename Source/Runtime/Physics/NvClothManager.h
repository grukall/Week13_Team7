#pragma once

namespace nv { namespace cloth { class Factory; } }
typedef struct CUctx_st* CUcontext;		// cuda.h와 같은 선언 (NvCloth/Factory.h도 같은 방식). 헤더에서 cuda.h를 끌어오지 않는다

// NvCloth 전역 초기화와 Factory 보관.
// CPU Factory는 항상 만들고, CUDA Factory는 빌드에 CUDA가 켜져 있고(NV_CLOTH_ENABLE_CUDA) 드라이버·장치가 있을 때만 만든다.
// 서로 다른 Factory로 만든 Solver·Fabric·Cloth는 섞어 쓸 수 없다 (NvCloth 문서). 천 컴포넌트는 Factory 하나를 골라 셋을 모두 그것으로 만든다.
class FNvClothManager
{
public:
	static FNvClothManager& Get();

	bool Initialize();
	void Shutdown();	// 모든 천 컴포넌트가 해제된 뒤에 부른다 (LaunchEngineLoop Exit: PreExit에서 월드 정리 → ShutdownClothManager)

	// bPreferCUDA가 true이고 CUDA Factory가 있으면 CUDA, 아니면 CPU
	nv::cloth::Factory* GetFactory(bool bPreferCUDA = false) const;
	bool IsCUDAAvailable() const { return CUDAFactory != nullptr; }
	bool IsCUDAFactory(const nv::cloth::Factory* Factory) const { return Factory != nullptr && Factory == CUDAFactory; }

	// 전역 솔버 선택 (에디터 Settings 패널의 체크박스). 모든 천 컴포넌트가 이 값을 따른다.
	// 값이 바뀌면 BackendSerial이 올라가고, 각 컴포넌트는 다음 Tick 시작에서 자기가 만든 번호와 다르면 Solver·천을 다시 만든다
	void SetUseCUDA(bool bInUseCUDA);
	bool GetUseCUDA() const { return bUseCUDA; }
	uint32 GetBackendSerial() const { return BackendSerial; }

private:
	FNvClothManager() = default;
	FNvClothManager(const FNvClothManager&) = delete;
	FNvClothManager& operator=(const FNvClothManager&) = delete;

	void InitializeCUDA();		// 실패해도 로그만 남기고 CPU만 쓴다
	void ShutdownCUDA();

	nv::cloth::Factory* CPUFactory = nullptr;
	nv::cloth::Factory* CUDAFactory = nullptr;
	CUcontext CudaContext = nullptr;	// CUDA Factory가 쓰는 컨텍스트. Factory를 지운 뒤 cuCtxDestroy

	bool bUseCUDA = false;		// 원하는 솔버. CUDA가 없으면 GetFactory가 CPU를 돌려준다
	uint32 BackendSerial = 0;	// bUseCUDA가 바뀔 때마다 증가
};
