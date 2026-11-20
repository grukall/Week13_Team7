#include "EnginePCH.h"
#include "NvClothManager.h"

#include <NvCloth/Callbacks.h> 
#include <NvCloth/Factory.h> 
#include <foundation/PxErrorCallback.h>

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
	nv::cloth::Factory* factory = NvClothCreateFactoryCPU();								// Factory 생성
	if (factory != nullptr)
	{
		Factory = factory;
		HTR_LOG(Info, "[NvCloth] Manager Initialize Success");
		return true;
	}
	HTR_LOG(Error, "[NvCloth] Manager Initialize Failed");	
	return false;
}


void FNvClothManager::Shutdown()
{
	if (Factory == nullptr) return;
	NvClothDestroyFactory(Factory);
	Factory = nullptr;
}

