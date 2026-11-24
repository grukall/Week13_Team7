#include "EnginePCH.h"
#include "NvClothManager.h"

#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <foundation/PxErrorCallback.h>

#if NV_CLOTH_ENABLE_CUDA
#include <cuda.h>	// 드라이버 API. nvcuda.dll은 지연 로드(/DELAYLOAD)이므로 cu*를 부르기 전에 LoadLibrary로 드라이버를 확인한다
#endif

namespace
{
	 class FNvClothAllocator : public physx::PxAllocatorCallback
	{  
	public:
		void* allocate(size_t size, const char* typeName, const char* filename, int line)  override
		{
			return _aligned_malloc(size, 16);			// 계산 시 SIMD를 라이브러리에서 사용하므로 16바이트 정렬 필수
		}

		void deallocate(void* ptr) override
		{
			_aligned_free(ptr);
		}
	};

	class FNvClothErrorCallback : public physx::PxErrorCallback 
	{ 
	public:
		void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override		// 에러 발생시 ErrorCode에 따라서 Log 출력
		{
			switch (code)
			{
			case physx::PxErrorCode::eDEBUG_INFO:
				HTR_LOG(Info, "[NvCloth] {} ({}:{})", message, file, line);
				break;
			case physx::PxErrorCode::eDEBUG_WARNING:
			case physx::PxErrorCode::ePERF_WARNING:
				HTR_LOG(Warning, "[NvCloth] {} ({}:{})", message, file, line);
				break;
			default:
				HTR_LOG(Error, "[NvCloth] {} ({}:{})", message, file, line);
				break;
			}
		}
	};

	class FNvClothAssertHandler : public nv::cloth::PxAssertHandler 
	{ 
	public:
		void operator()(const char* exp, const char* file, int line, bool& ignore) override		
		{
			HTR_LOG(Error, "[NvCloth] Assert Failed: {} ({}:{})", exp, file, line);	// Assert 발생시 오류 출력
			if (IsDebuggerPresent())  __debugbreak();		// 디버거 살아있으면 Handler에서 중단
		}
	};

	FNvClothAllocator     GAllocator;     
	FNvClothErrorCallback GErrorCallback;
	FNvClothAssertHandler GAssertHandler;
}

FNvClothManager& FNvClothManager::Get()
{
	static FNvClothManager Instance;
	return Instance;
}

bool FNvClothManager::Initialize()
{
	nv::cloth::InitializeNvCloth(&GAllocator, &GErrorCallback, &GAssertHandler,nullptr);	// 초기 NvCloth 초기화, 메모리 할당, 오류 처리 등을 수행할 Callback 세팅
	CPUFactory = NvClothCreateFactoryCPU();								// CPU Factory는 항상 만든다 (CUDA 실패 시 대체)
	if (CPUFactory == nullptr)
	{
		HTR_LOG(Error, "[NvCloth] Manager Initialize Failed");
		return false;
	}

	InitializeCUDA();
	HTR_LOG(Info, "[NvCloth] Manager Initialize Success (CUDA {})", IsCUDAAvailable() ? "available" : "unavailable");
	return true;
}

nv::cloth::Factory* FNvClothManager::GetFactory(bool bPreferCUDA) const
{
	return (bPreferCUDA && CUDAFactory != nullptr) ? CUDAFactory : CPUFactory;
}

void FNvClothManager::SetUseCUDA(bool bInUseCUDA)
{
	if (bUseCUDA == bInUseCUDA) return;
	bUseCUDA = bInUseCUDA;
	++BackendSerial;	// 컴포넌트들이 다음 Tick에 감지해 다시 만든다
	HTR_LOG(Info, "[NvCloth] Solver backend set to {}{}", bUseCUDA ? "CUDA" : "CPU",
		(bUseCUDA && !IsCUDAAvailable()) ? " (unavailable, CPU will be used)" : "");
}

void FNvClothManager::InitializeCUDA()
{
#if NV_CLOTH_ENABLE_CUDA
	// 1. 드라이버 확인. nvcuda.dll은 지연 로드라서, 없는 상태로 cu*를 부르면 지연 로드 예외로 종료된다
	if (LoadLibraryW(L"nvcuda.dll") == nullptr)
	{
		HTR_LOG(Warning, "[NvCloth] nvcuda.dll not found (no NVIDIA driver). Using CPU solver only.");
		return;
	}

	// 2. 장치 확인과 컨텍스트 생성 (NvCloth 문서의 CUDA Factory 예시와 같은 순서)
	int DeviceCount = 0;
	CUdevice Device = 0;
	if (cuInit(0) != CUDA_SUCCESS || cuDeviceGetCount(&DeviceCount) != CUDA_SUCCESS || DeviceCount < 1 ||
		cuDeviceGet(&Device, 0) != CUDA_SUCCESS)
	{
		HTR_LOG(Warning, "[NvCloth] No CUDA device. Using CPU solver only.");
		return;
	}
	if (cuCtxCreate(&CudaContext, 0, Device) != CUDA_SUCCESS)
	{
		CudaContext = nullptr;
		HTR_LOG(Warning, "[NvCloth] cuCtxCreate failed. Using CPU solver only.");
		return;
	}

	// 3. Factory 생성. CuFactory 생성자는 컨텍스트를 push하지 않고 커널(fatbinary)을 불러오므로,
	//    cuCtxCreate가 현재 스레드에 올려 둔 컨텍스트가 살아 있는 동안 만든다. 이후 호출은 NvCloth가 CuContextLock으로 push/pop한다
	CUDAFactory = NvClothCreateFactoryCUDA(CudaContext);
	cuCtxPopCurrent(nullptr);		// 메인 스레드에 컨텍스트를 남겨 두지 않는다

	if (CUDAFactory == nullptr)
	{
		cuCtxDestroy(CudaContext);
		CudaContext = nullptr;
		HTR_LOG(Warning, "[NvCloth] CUDA factory creation failed. Using CPU solver only.");
		return;
	}

	char DeviceName[256] = {};
	cuDeviceGetName(DeviceName, static_cast<int>(sizeof(DeviceName)), Device);
	HTR_LOG(Info, "[NvCloth] CUDA factory created on {}", DeviceName);
#endif
}

void FNvClothManager::ShutdownCUDA()
{
#if NV_CLOTH_ENABLE_CUDA
	if (CUDAFactory != nullptr)
	{
		NvClothDestroyFactory(CUDAFactory);
		CUDAFactory = nullptr;
	}
	if (CudaContext != nullptr)
	{
		cuCtxDestroy(CudaContext);	// NvCloth 문서: Factory를 지운 뒤에 컨텍스트를 지운다
		CudaContext = nullptr;
	}
#endif
}

void FNvClothManager::Shutdown()
{
	ShutdownCUDA();
	if (CPUFactory == nullptr) return;
	NvClothDestroyFactory(CPUFactory);
	CPUFactory = nullptr;
}

