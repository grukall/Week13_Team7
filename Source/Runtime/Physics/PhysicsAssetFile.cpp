#include "EnginePCH.h"
#include "Physics/PhysicsAssetFile.h"

#include <fstream>
#include <limits>

#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "Core/Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

bool PhysicsAssetFile::Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicsAsset: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicsAsset: cannot read file";
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: invalid JSON";
		return false;
	}
	FStructuredArchive Archive(Formatter);
	Asset.Serialize(Archive.Open().EnterRecord());
	Archive.Close();
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: unsupported version or broken data";
		return false;
	}
	return true;
}

bool PhysicsAssetFile::Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Asset.Serialize(Archive.Open().EnterRecord());
	}
	if (Writer.IsError())
	{
		OutError = "PhysicsAsset: cannot serialize data";
		return false;
	}

	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicsAsset: cannot open output file";
		return false;
	}
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicsAsset: cannot write file";
		return false;
	}
	return true;
}

std::filesystem::path PhysicsAssetFile::GetPathForMesh(const FString& MeshPath)
{
	FString Base = MeshPath;
	// ".fbx.bin"은 확장자가 두 겹이라 replace_extension으로는 ".fbx"가 남는다
	if (Base.size() >= 8)
	{
		FString End = Base.substr(Base.size() - 8);
		for (char& C : End) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
		if (End == ".fbx.bin")
			Base.resize(Base.size() - 8);
	}
	std::filesystem::path Path = FPaths::FromUtf8(Base);
	Path.replace_extension(".physasset");
	return Path;
}

UPhysicsAsset* PhysicsAssetFile::LoadOrCreate(const FString& MeshPath, const USkeletalMesh* Mesh)
{
	if (!Mesh)
		return nullptr;

	const std::filesystem::path Path = GetPathForMesh(MeshPath);
	std::error_code Ec;
	if (std::filesystem::exists(Path, Ec))
	{
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();
		FString Error;
		if (Read(Path, *Asset, Error))
		{
			// 메시를 다시 임포트해 본 이름이 바뀌었으면 그 바디/관절은 InitArticulated에서 건너뛴다
			int32 Missing = 0;
			for (const USkeletalBodySetup* Setup : Asset->SkeletalBodySetups)
				if (Setup && Mesh->FindBoneIndex(Setup->BoneName) == -1)
					++Missing;
			if (Missing > 0)
				HTR_LOG(Warning, "PhysicsAsset: {} bodies reference bones missing from the mesh ({})", Missing, FPaths::ToUtf8(Path));
			return Asset;
		}
		// 깨진 파일은 덮어쓰지 않는다 (손으로 고친 내용일 수 있으니 사람이 확인하게 둔다)
		HTR_LOG(Warning, "Failed to load PhysicsAsset {} ({}), using generated asset without saving", FPaths::ToUtf8(Path), Error);
		delete Asset;
		return UPhysicsAsset::CreateFromSkeletalMesh(Mesh);
	}

	// 파일이 없으면 생성하고 메시 옆에 저장한다. 다음 로드부터는 이 파일을 읽는다
	UPhysicsAsset* Generated = UPhysicsAsset::CreateFromSkeletalMesh(Mesh);
	if (Generated)
	{
		FString Error;
		if (Write(Path, *Generated, Error))
			HTR_LOG(Info, "PhysicsAsset generated and saved: {}", FPaths::ToUtf8(Path));
		else
			HTR_LOG(Warning, "PhysicsAsset generated but not saved: {} ({})", FPaths::ToUtf8(Path), Error);
	}
	return Generated;
}
