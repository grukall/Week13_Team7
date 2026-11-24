#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Panels/PhysicsAssetEditorPanel.h"

class FPhysicsAssetEditor;

class FPhysicsAssetEditorTab : public FAssetEditorTab
{
public:
	explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;

	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

private:
	// 탭 맨 위 툴바 (레벨 에디터처럼 왼쪽 위 저장 버튼)
	void DrawToolbar();
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FPhysicsAssetEditor& AssetEditor;
	TArray<TUniquePtr<FPhysicsAssetEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
