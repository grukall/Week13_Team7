#include "EnginePCH.h"
#include "PhysicsAssetToolPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Physics/PhysicsAsset.h"

namespace
{
	constexpr float LabelColumnRatio = 0.45f;   // 라벨 | 값 비율 (디테일 패널과 같다)

	void BeginRow(const char* Label)
	{
		const float StartX = ImGui::GetCursorPosX();
		const float Available = ImGui::GetContentRegionAvail().x;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::SameLine();
		ImGui::SetCursorPosX(StartX + Available * LabelColumnRatio);
		ImGui::SetNextItemWidth(-1.0f);
	}

	template <typename TEnum>
	bool EnumCombo(const char* Id, TEnum& Value, const char* const* Names, int32 Count)
	{
		int32 Index = static_cast<int32>(Value);
		if (ImGui::Combo(Id, &Index, Names, Count))
		{
			Value = static_cast<TEnum>(Index);
			return true;
		}
		return false;
	}
}

FPhysicsAssetToolPanel::FPhysicsAssetToolPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Tool")
{
}

void FPhysicsAssetToolPanel::DrawContent()
{
	FPhysAssetCreateParams& Params = Editor.GetCreateParams();

	if (ImGui::CollapsingHeader("Body Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 크기 기준은 "모든 본에 바디 생성"을 켜면 쓰이지 않는다
		ImGui::BeginDisabled(Params.bBodyForAll);
		BeginRow("Min Bone Size");
		ImGui::DragFloat("##MinBoneSize", &Params.MinBoneSize, 0.005f, 0.0f, 10.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Bones whose capsule (length + diameter) is shorter than this get no body.");

		BeginRow("Min Bone Radius");
		ImGui::DragFloat("##MinBoneRadius", &Params.MinBoneRadius, 0.001f, 0.0f, 1.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Bones thinner than this (e.g. fingers) get no body.");
		ImGui::EndDisabled();

		static const char* GeomNames[] = { "Capsule", "Box", "Sphere" };   // EPhysAssetFitGeomType 순서
		BeginRow("Primitive Type");
		EnumCombo("##GeomType", Params.GeomType, GeomNames, IM_ARRAYSIZE(GeomNames));

		static const char* WeightNames[] = { "Any Weight", "Dominant Weight" };
		BeginRow("Vertex Weighting");
		EnumCombo("##VertWeight", Params.VertWeight, WeightNames, IM_ARRAYSIZE(WeightNames));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Any: every bone influencing a vertex grows by it (bodies overlap).\nDominant: only the strongest bone (tighter bodies).");

		BeginRow("Create Body For All Bones");
		ImGui::Checkbox("##BodyForAll", &Params.bBodyForAll);
	}

	if (ImGui::CollapsingHeader("Constraint Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		BeginRow("Create Constraints");
		ImGui::Checkbox("##CreateConstraints", &Params.bCreateConstraints);

		static const char* MotionNames[] = { "Free", "Limited", "Locked" };
		ImGui::BeginDisabled(!Params.bCreateConstraints);
		BeginRow("Angular Constraint Mode");
		EnumCombo("##AngularMode", Params.AngularConstraintMode, MotionNames, IM_ARRAYSIZE(MotionNames));
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	if (ImGui::Button("Reset to Defaults"))
	{
		Params = FPhysAssetCreateParams();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Apply with Regenerate Bodies (toolbar)");
}
