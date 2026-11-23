#include "EnginePCH.h"
#include "PhysicsAssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

#include <imgui_internal.h>

FPhysicsAssetEditorPanel::FPhysicsAssetEditorPanel(FPhysicsAssetEditor& InEditor, const char* InName)
	: Editor(InEditor)
	, PanelName(InName)
	// ### 뒤 ID에 에셋 경로를 넣어 같은 패널이라도 에셋마다 다른 창이 되게 한다
	, WindowName(PanelName + "###PhysicsAssetPanel:" + InName + ":" + InEditor.GetAssetPath())
{
}

void FPhysicsAssetEditorPanel::OnRender()
{
	// 같은 에디터의 패널끼리만 도킹되고 다른 탭의 도킹 영역과 섞이지 않게 한다
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = DockClassId;
	WindowClass.DockingAllowUnclassed = false;
	WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
	ImGui::SetNextWindowClass(&WindowClass);
	ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);

	if (EditorPanelUI::BeginPanel(GetWindowName(), GetOpenFlag()))
	{
		DrawContent();
	}
	ImGui::End();
}

bool FPhysicsAssetEditorPanel::IsDetached(ImGuiID DockSpaceId) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(GetWindowName());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != DockSpaceId);
}
