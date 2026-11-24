#include "EnginePCH.h"
#include "PhysicsAssetConstraintsPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Physics/SkeletalBodySetup.h"

namespace
{
	using ESelection = FPhysicsAssetEditor::EPhysicsSelection;

	// 관절 Bone1 = 자식, Bone2 = 부모
	bool ConnectsTo(const UPhysicsConstraintTemplate* Template, const FString& BoneName)
	{
		return Template && (Template->DefaultInstance.ConstraintBone1 == BoneName || Template->DefaultInstance.ConstraintBone2 == BoneName);
	}
}

FPhysicsAssetConstraintsPanel::FPhysicsAssetConstraintsPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Constraints")
{
}

void FPhysicsAssetConstraintsPanel::UpdateFocusBody(const UPhysicsAsset& Asset)
{
	const int32 Index = Editor.GetSelectedIndex();
	switch (Editor.GetSelectionType())
	{
	case ESelection::Body:
		if (Asset.SkeletalBodySetups.IsValidIndex(Index) && Asset.SkeletalBodySetups[Index])
		{
			FocusBoneName = Asset.SkeletalBodySetups[Index]->BoneName;
		}
		break;
	case ESelection::Constraint:
		if (Asset.ConstraintSetup.IsValidIndex(Index) && !ConnectsTo(Asset.ConstraintSetup[Index], FocusBoneName))
		{
			FocusBoneName = Asset.ConstraintSetup[Index]->DefaultInstance.ConstraintBone1;
		}
		break;
	default:
		break;   // 선택을 풀어도 목록은 남겨 둔다
	}

	// 기준 바디가 지워졌으면 비운다
	if (!FocusBoneName.empty() && Asset.FindBodyIndex(FocusBoneName) < 0)
	{
		FocusBoneName.clear();
	}
}

void FPhysicsAssetConstraintsPanel::DrawContent()
{
	const UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	if (!Asset)
	{
		return;
	}
	UpdateFocusBody(*Asset);

	if (FocusBoneName.empty())
	{
		ImGui::TextDisabled("Select a body to see its constraints.");
		return;
	}

	ImGui::TextDisabled("Body:");
	ImGui::SameLine();
	ImGui::TextUnformatted(FocusBoneName.c_str());
	ImGui::Separator();

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
	int32 RowCount = 0;
	if (ImGui::BeginTable("##Constraints", 2, TableFlags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Connected Body", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Parent").x + 8.0f);
		ImGui::TableHeadersRow();

		// 부모 쪽 관절(기준 바디가 자식)을 먼저, 그다음 자식 쪽 관절을 그린다
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bParentPass = Pass == 0;
			for (int32 i = 0; i < Asset->ConstraintSetup.Num(); ++i)
			{
				const UPhysicsConstraintTemplate* Template = Asset->ConstraintSetup[i];
				if (!Template)
				{
					continue;
				}
				const FConstraintInstance& CI = Template->DefaultInstance;
				const bool bFocusIsChild = CI.ConstraintBone1 == FocusBoneName;
				const bool bFocusIsParent = CI.ConstraintBone2 == FocusBoneName;
				if (bParentPass ? !bFocusIsChild : !bFocusIsParent)
				{
					continue;
				}
				const FString& OtherBone = bParentPass ? CI.ConstraintBone2 : CI.ConstraintBone1;
				++RowCount;

				ImGui::PushID(i);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				const bool bSelected = Editor.GetSelectionType() == ESelection::Constraint && Editor.GetSelectedIndex() == i;
				if (ImGui::Selectable(OtherBone.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns))
				{
					Editor.SetSelection(ESelection::Constraint, i);
				}
				// 우클릭: 그 관절을 고르고 메뉴를 연다
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					Editor.SetSelection(ESelection::Constraint, i);
				}
				if (ImGui::BeginPopupContextItem("ConstraintContextMenu"))
				{
					ImGui::TextDisabled("%s - %s", CI.ConstraintBone2.c_str(), CI.ConstraintBone1.c_str());
					ImGui::Separator();
					if (ImGui::MenuItem("Delete Constraint", "Delete"))
					{
						PendingDeleteConstraint = i;
					}
					ImGui::EndPopup();
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled(bParentPass ? "Parent" : "Child");
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}

	if (RowCount == 0)
	{
		ImGui::TextDisabled("No constraints");
	}

	// 목록을 다 그린 뒤에 지운다 (그리는 중에 배열을 바꾸지 않게)
	if (PendingDeleteConstraint >= 0)
	{
		Editor.DeleteConstraint(PendingDeleteConstraint);
		PendingDeleteConstraint = -1;
	}

	// Delete 키: 이 패널에 포커스가 있을 때 선택한 관절을 지운다
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
		Editor.GetSelectionType() == ESelection::Constraint && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		Editor.DeleteSelection();
	}
}
