#include "EnginePCH.h"
#include "PhysicsAssetSettingsPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

FPhysicsAssetSettingsPanel::FPhysicsAssetSettingsPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Settings")
{
}

void FPhysicsAssetSettingsPanel::DrawContent()
{
	ImGui::SeparatorText("Camera");

	// WASD · QE 이동과 휠 줌 속도 (FSkeletalPreviewEditor::SetCameraSpeed가 1 ~ 150으로 묶는다)
	float CameraSpeed = Editor.GetCameraSpeed();
	ImGui::TextUnformatted("Camera Speed");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##CameraSpeed", &CameraSpeed, 1.0f, 150.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
	{
		Editor.SetCameraSpeed(CameraSpeed);
	}

	// 우클릭 드래그로 돌릴 때 마우스 1픽셀당 회전 각도 (도)
	float RotationSensitivity = Editor.GetCameraRotationSensitivity();
	ImGui::TextUnformatted("Rotation Sensitivity");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##RotationSensitivity", &RotationSensitivity, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
	{
		Editor.SetCameraRotationSensitivity(RotationSensitivity);
	}

	if (ImGui::Button("Reset Camera Settings"))
	{
		Editor.SetCameraSpeed(5.0f);
		Editor.SetCameraRotationSensitivity(0.2f);
	}
}
