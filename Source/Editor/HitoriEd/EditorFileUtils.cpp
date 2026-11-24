#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
#include "imgui.h"
 
FString FEditorFileUtils::CurrentScenePath = "";

namespace
{
	struct FPendingFbxImport
	{
		bool bOpenPopup = false;
		FString FilePath;
		EFbxImportMode Mode = EFbxImportMode::Static;
		float ImportUniformScale = 1.0f;
		bool bForceReimport = true;
	};

	FPendingFbxImport GPendingFbxImport;
}

void FEditorFileUtils::ImportFbx(EFbxImportMode Mode)
{
	wchar_t FilePath[32768] = {};
	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = GetActiveWindow();
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(FilePath));
	Dialog.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0";
	Dialog.lpstrTitle = Mode == EFbxImportMode::Static ? L"Import Static Mesh" : L"Import Skeletal Mesh";
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&Dialog))
	{
		const DWORD Error = CommDlgExtendedError();
		if (Error != 0) HTR_LOG(Error, "FBX file dialog failed: {}", Error);
		return;
	}

	// 파일 선택의 UTF-16 경로를 Importer가 받는 UTF-8로 변환해 옵션 팝업을 연다.
	const auto Utf8 = std::filesystem::path(FilePath).generic_u8string();
	GPendingFbxImport.FilePath = FString(Utf8.begin(), Utf8.end());
	GPendingFbxImport.Mode = Mode;
	GPendingFbxImport.bForceReimport = true;
	GPendingFbxImport.bOpenPopup = true;
}

void FEditorFileUtils::DrawFbxImportPopup()
{
	constexpr const char* PopupId = "FBX Import Options";
	if (GPendingFbxImport.bOpenPopup)
	{
		ImGui::OpenPopup(PopupId);
		GPendingFbxImport.bOpenPopup = false;
	}

	if (!ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	const char* ModeLabel = GPendingFbxImport.Mode == EFbxImportMode::Static ? "Static Mesh" : "Skeletal Mesh";
	const FString FileName = std::filesystem::path(
		std::u8string(GPendingFbxImport.FilePath.begin(), GPendingFbxImport.FilePath.end())).filename().string();

	ImGui::Text("Mode: %s", ModeLabel);
	ImGui::Text("File: %s", FileName.c_str());
	ImGui::Separator();

	ImGui::InputFloat("Import Uniform Scale", &GPendingFbxImport.ImportUniformScale, 0.01f, 0.1f, "%.4f");
	if (GPendingFbxImport.ImportUniformScale <= 0.0001f)
	{
		GPendingFbxImport.ImportUniformScale = 0.0001f;
	}

	ImGui::TextUnformatted("Presets:");
	ImGui::SameLine();
	if (ImGui::SmallButton("1.0 (cm)")) GPendingFbxImport.ImportUniformScale = 1.0f;
	ImGui::SameLine();
	if (ImGui::SmallButton("0.01 (m / Vehicle)")) GPendingFbxImport.ImportUniformScale = 0.01f;
	ImGui::SameLine();
	if (ImGui::SmallButton("0.1")) GPendingFbxImport.ImportUniformScale = 0.1f;

	ImGui::Checkbox("Force Reimport (overwrite existing)", &GPendingFbxImport.bForceReimport);
	ImGui::Separator();

	if (ImGui::Button("Import", ImVec2(120, 0)))
	{
		FFbxImportOptions Options;
		Options.ImportUniformScale = GPendingFbxImport.ImportUniformScale;
		Options.bForceReimport = GPendingFbxImport.bForceReimport;

		FFbxImportResult Result;
		FString Error;
		if (!FFbxImporter::ImportToAssets(GPendingFbxImport.FilePath, GPendingFbxImport.Mode, Result, Error, Options))
		{
			HTR_LOG(Error, "FBX Import failed: {}", Error);
			MessageBoxW(GetActiveWindow(), L"FBX를 가져오지 못했습니다. Output Log에서 오류를 확인하세요.", L"FBX Import", MB_OK | MB_ICONERROR);
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
		return false;

	World->ClearWorld();

	CurrentScenePath.clear();

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
		return false;

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);

		return false;
	}

	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::LoadWorld(World, FilePath))
		return false;

	// 이제 이 파일이 현재 Scene
	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = "scene";
	ofn.lpstrInitialDir = "Scene/";

	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_OVERWRITEPROMPT |
		OFN_NOCHANGEDIR;

	if (GetSaveFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter =
		"Scene Files (*.scene)\0*.scene\0"
		"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = "scene";
	ofn.lpstrInitialDir = "Scene/";

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}


