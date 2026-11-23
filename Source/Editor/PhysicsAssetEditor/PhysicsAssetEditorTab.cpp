#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"


FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FAssetEditorTab(InEditor),
	AssetEditor(InEditor)
{
	//필요한 panel 추가
}

void FPhysicsAssetEditorTab::DrawMenuBar()
{
	// 기본 "Editor > Close Editor" 메뉴 유지
	FAssetEditorTab::DrawMenuBar();

}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2 & Size)
{}
