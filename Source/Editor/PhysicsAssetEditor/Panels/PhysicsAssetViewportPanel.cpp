#include "EnginePCH.h"
#include "PhysicsAssetViewportPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/Viewports/EditorViewportToolbar.h"

FPhysicsAssetViewportPanel::FPhysicsAssetViewportPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Viewport")
{
}

void FPhysicsAssetViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	// 1. 에디터가 그려 둔 렌더 타깃을 패널에 띄운다
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);
	}

	// 2. 뷰포트 툴바 (뷰 종류, 카메라, 뷰 모드, 쇼 플래그). 기즈모 버튼은 편집 기능을 붙일 때 넘긴다.
	//    입력 영역보다 먼저 그려야 툴바 버튼이 마우스를 먼저 받는다
	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax);

	// 3. 이미지 영역을 입력 항목으로 등록해 드래그 중에도 카메라 조작이 유지되게 한다
	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##PhysicsAssetViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
	{
		EditorViewportUI::HandleViewTypeShortcuts(Client);
		if (ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			Editor.FocusMesh();
		}
	}

	// 4. 입력과 패널 크기를 에디터에 넘긴다. 이 크기로 다음 Render에서 렌더 타깃을 만든다
	const ImGuiIO& IO = ImGui::GetIO();
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bGizmoDragging = Editor.IsGizmoDragging();

	const FViewportPointerInput PointerInput
	{
		.MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
		.bHovered = bHovered,
		.bFocused = bFocused && !IO.AppFocusLost,
		.bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
	};

	const FViewportCameraInput Input
	{
		.bCaptured   = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !bGizmoDragging,
		.bAllowWheel = bHovered && !bMouseDown && !bGizmoDragging,
		.bForward    = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward   = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight      = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft       = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp         = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown       = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX      = IO.MouseDelta.x,
		.MouseY      = IO.MouseDelta.y,
		.Wheel       = IO.MouseWheel * 120.0f,   // ImGui 휠 단위 → Windows 휠 단위
	};

	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), Input, PointerInput);
}
