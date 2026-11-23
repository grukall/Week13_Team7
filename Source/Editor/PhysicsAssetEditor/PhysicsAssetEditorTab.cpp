#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Panels/PhysicsAssetViewportPanel.h"

#include <imgui_internal.h>

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, AssetEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 에셋별 도킹 ID가 유지되도록 경로로 해시한다
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("PhysicsAssetDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("PhysicsAssetPanelClass", 0, AssetId);

	// Panels[0]은 항상 뷰포트 (BuildDefaultLayout이 인덱스로 배치한다)
	Panels.Add(MakeUnique<FPhysicsAssetViewportPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FPhysicsAssetEditorTab::DrawMenuBar()
{
	// 기본 "Editor > Close Editor" 메뉴 유지
	FAssetEditorTab::DrawMenuBar();

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
			{
				Panel->SetOpen(!Panel->IsOpen());
			}
		}
		ImGui::EndMenu();
	}
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	// 지금은 뷰포트 하나가 전체를 차지한다.
	// 스켈레톤 트리 · 디테일 패널을 추가하면 여기서 DockBuilderSplitNode로 오른쪽을 나눈다
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), InDockSpaceId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{
	// 패널이 안 그려지는 프레임에는 크기 0을 넘겨 에디터가 렌더링·입력을 쉬게 한다.
	// 뷰포트 패널이 그려지면 아래에서 실제 크기로 덮어쓴다
	AssetEditor.SetViewportState(0, 0, {});
	if (AssetEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 지우고 기본 레이아웃으로 시작한다
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
		{
			EditorPanelUI::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
		}
	}

	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 따로 표시한다
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}
}
