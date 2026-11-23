#pragma once

#include "Core/EngineString.h"
#include <filesystem>

class UPhysicsAsset;
class USkeletalMesh;

// .physasset (JSON) 읽기/쓰기. ParticleSystemFile과 같은 구조
namespace PhysicsAssetFile
{
	bool Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
	bool Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);

	// "Foo.fbx.bin" → "Foo.physasset" (메시 파일 옆에 둔다)
	std::filesystem::path GetPathForMesh(const FString& MeshPath);

	// 메시 옆에 .physasset이 있으면 읽는다. 없으면 본 정보로 생성해 그 자리에 저장하고,
	// 파일이 깨져 있으면 덮어쓰지 않고 생성값만 메모리에서 쓴다
	UPhysicsAsset* LoadOrCreate(const FString& MeshPath, const USkeletalMesh* Mesh);
}
