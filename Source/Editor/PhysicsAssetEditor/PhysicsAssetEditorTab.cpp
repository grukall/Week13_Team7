#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Panels/PhysicsAssetViewportPanel.h"
#include "Panels/PhysicsAssetSkeletonTreePanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetSettingsPanel.h"
#include "Panels/PhysicsAssetToolPanel.h"
#include "Panels/PhysicsAssetConstraintsPanel.h"
#include "Editor/EditorUI/EditorIcons.h"

#include <imgui_internal.h>

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, AssetEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 에셋별 도킹 ID가 유지되도록 경로로 해시한다
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("PhysicsAssetDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("PhysicsAssetPanelClass", 0, AssetId);

	// 순서 = BuildDefaultLayout이 쓰는 인덱스. [0] 뷰포트, [1] 스켈레톤 트리, [2] 디테일, [3] 설정, [4] 툴
	Panels.Add(MakeUnique<FPhysicsAssetViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetSkeletonTreePanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetSettingsPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetToolPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetConstraintsPanel>(InEditor));
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

void FPhysicsAssetEditorTab::DrawToolbar()
{
	const float S = EditorIcons::GetScale();
	ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f * S);

	// 레벨 에디터 툴바와 같은 아이콘 버튼. 바뀐 게 없으면 흐리게 막아 둔다
	ImGui::BeginDisabled(!AssetEditor.IsDirty());
	if (EditorIcons::Button("##SavePhysicsAsset", EEditorIcon::Save, ImGui::GetColorU32(ImGuiCol_Text)))
	{
		AssetEditor.Save();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Save (Ctrl+S)");
	}

	// 전체 바디 재생성: Tool 패널 옵션으로 바디 · 관절을 모두 다시 만든다. 손으로 고친 값이 사라지니 한 번 묻는다
	ImGui::SameLine(0.0f, 4.0f * S);
	if (EditorIcons::Button("##RegenerateBodies", EEditorIcon::Reset, ImGui::GetColorU32(ImGuiCol_Text), "Regenerate Bodies"))
	{
		ImGui::OpenPopup("Regenerate Bodies?");
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Regenerate all bodies and constraints with the Tool panel settings.");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Regenerate Bodies?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("All bodies and constraints will be replaced.");
		ImGui::TextDisabled("Edits made in this editor are lost. The file changes only when you save.");
		ImGui::Spacing();
		if (ImGui::Button("Regenerate", ImVec2(120.0f * S, 0.0f)))
		{
			AssetEditor.RegenerateAllBodies();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f * S, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// 실패했을 때만 이유를 옆에 띄운다 (성공은 탭 이름의 *가 바뀌는 것으로 보인다)
	const FString& Message = AssetEditor.GetSaveMessage();
	if (Message.find("failed") != FString::npos)
	{
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Message.c_str());
	}

	ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
	ImGui::Separator();
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	// 왼쪽 22% 스켈레톤 트리, 오른쪽 28% 디테일(아래 40%는 툴 · 설정 탭), 가운데 뷰포트 (UE Physics Asset Editor와 같은 배치)
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.22f, nullptr, &ViewportId);
	// 트리 아래 35%: 선택한 바디의 관절 목록
	const ImGuiID ConstraintsId = ImGui::DockBuilderSplitNode(TreeId, ImGuiDir_Down, 0.35f, nullptr, &TreeId);
	ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.36f, nullptr, &ViewportId);
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.4f, nullptr, &DetailsId);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), SettingsId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), SettingsId);
	ImGui::DockBuilderDockWindow(Panels[5]->GetWindowName(), ConstraintsId);
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

	// 툴바를 먼저 그려야 아래 독스페이스가 툴바 아래 남은 영역을 채운다
	if (bActive)
	{
		DrawToolbar();

		// Ctrl+S: 이 탭이 보이고 있을 때만 (레벨 에디터 등 다른 탭과 겹치지 않게)
		const ImGuiIO& IO = ImGui::GetIO();
		if (IO.KeyCtrl && !IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false) && AssetEditor.IsDirty())
		{
			AssetEditor.Save();
		}
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
