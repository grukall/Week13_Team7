#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"

class FPhysicsAssetEditor;

// PhysicsAsset 에디터 패널의 창 생성, 닫기 상태, 도킹을 공통으로 관리한다 (FSkeletalMeshEditorPanel과 같은 구조)
class FPhysicsAssetEditorPanel : public IEditorPanel
{
public:
	FPhysicsAssetEditorPanel(FPhysicsAssetEditor& InEditor, const char* InName);

	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;

	const char* GetPanelName() const override { return PanelName.c_str(); }
	const char* GetWindowName() const { return WindowName.c_str(); }
	void SetDockClass(ImGuiID InClassId) { DockClassId = InClassId; }

	// 패널이 소속 탭의 도킹 영역 밖으로 분리되었는지
	bool IsDetached(ImGuiID DockSpaceId) const;

protected:
	// 파생 패널은 Begin/End 없이 창 내부만 그린다
	virtual void DrawContent() = 0;

	FPhysicsAssetEditor& Editor;

private:
	FString PanelName;
	FString WindowName;
	ImGuiID DockClassId = 0;
};
