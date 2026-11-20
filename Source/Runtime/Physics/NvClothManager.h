#pragma once

namespace nv { namespace cloth { class Factory; } }

class FNvClothManager
{
public:
	static FNvClothManager& Get();

	bool Initialize();
	void Shutdown();

	nv::cloth::Factory* GetFactory() const { return Factory; }

private:
	FNvClothManager() = default;
	FNvClothManager(const FNvClothManager&) = delete;
	FNvClothManager& operator=(const FNvClothManager&) = delete;

	nv::cloth::Factory* Factory = nullptr;
};
