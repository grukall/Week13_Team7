#include "EnginePCH.h"
#include "PhysicsAssetSkeletonTreePanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
	using ESelection = FPhysicsAssetEditor::EPhysicsSelection;

	// 행 배치 (배율 1 기준 픽셀)
	constexpr float RowPadding = 6.0f;     // 패널 왼쪽 여백
	constexpr float IndentWidth = 16.0f;   // 깊이 한 칸
	constexpr float IconSize = 14.0f;
	constexpr float IconTextGap = 5.0f;
}

FPhysicsAssetSkeletonTreePanel::FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Skeleton Tree")
{
}

void FPhysicsAssetSkeletonTreePanel::DrawContent()
{
	const UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	if (!Editor.GetMesh() || !Asset)
	{
		return;
	}

	ImGui::TextDisabled("%d bodies", Asset->SkeletalBodySetups.Num());
	ImGui::Separator();

	const TArray<FEditorBoneItem>& BoneItems = Editor.GetBoneItems();
	BodyIndexByBone.Init(-1, BoneItems.Num());
	for (int32 BoneIndex = 0; BoneIndex < BoneItems.Num(); ++BoneIndex)
	{
		BodyIndexByBone[BoneIndex] = Asset->FindBodyIndex(BoneItems[BoneIndex].Name);
	}
	if (CollapsedBones.Num() != BoneItems.Num())
	{
		CollapsedBones.Init(0, BoneItems.Num());
	}
	UpdateSelectionTracking(*Asset);

	if (ImGui::BeginChild("PhysicsSkeletonTree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

		// 루트 본에 바디가 없을 수 있으니 "가장 위의 바디들"부터 그린다
		TArray<int32> TopBodies;
		for (int32 RootBoneIndex : Editor.GetRootBoneIndices())
		{
			if (BodyIndexByBone[RootBoneIndex] != -1)
			{
				TopBodies.Add(RootBoneIndex);
			}
			else
			{
				CollectChildBodies(RootBoneIndex, TopBodies);
			}
		}
		for (int32 BodyBone : TopBodies)
		{
			DrawBody(BodyBone, 0);
		}

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	bScrollToSelection = false;
}

void FPhysicsAssetSkeletonTreePanel::CollectChildBodies(int32 BoneIndex, TArray<int32>& OutBodyBones) const
{
	for (int32 Child : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
	{
		if (BodyIndexByBone[Child] != -1)
		{
			OutBodyBones.Add(Child);
		}
		else
		{
			CollectChildBodies(Child, OutBodyBones);
		}
	}
}

int32 FPhysicsAssetSkeletonTreePanel::CountBodiesBelow(int32 BoneIndex) const
{
	int32 Count = 0;
	for (int32 Child : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
	{
		Count += (BodyIndexByBone[Child] != -1 ? 1 : 0) + CountBodiesBelow(Child);
	}
	return Count;
}

void FPhysicsAssetSkeletonTreePanel::UpdateSelectionTracking(const UPhysicsAsset& Asset)
{
	const int32 Type = static_cast<int32>(Editor.GetSelectionType());
	const int32 Index = Editor.GetSelectedIndex();
	if (Type == LastSelectionType && Index == LastSelectedIndex)
	{
		return;
	}
	LastSelectionType = Type;
	LastSelectedIndex = Index;

	// 바디 선택만 따라간다 (관절은 그래프 패널에서 보여 준다)
	if (Editor.GetSelectionType() != ESelection::Body || !Asset.SkeletalBodySetups.IsValidIndex(Index) || !Asset.SkeletalBodySetups[Index])
	{
		return;
	}

	int32 Bone = Editor.GetMesh()->FindBoneIndex(Asset.SkeletalBodySetups[Index]->BoneName);
	if (Bone == -1)
	{
		return;
	}

	// 조상이 접혀 있으면 선택한 행이 안 보이니 펼친다
	const TArray<FBoneInfo>& Bones = Editor.GetMesh()->GetRefSkeleton();
	for (Bone = Bones[Bone].ParentIndex; Bone != -1; Bone = Bones[Bone].ParentIndex)
	{
		CollapsedBones[Bone] = 0;
	}
	bScrollToSelection = true;
}

float FPhysicsAssetSkeletonTreePanel::DrawBody(int32 BoneIndex, int32 Depth)
{
	const FEditorBoneItem& Item = Editor.GetBoneItems()[BoneIndex];
	const int32 BodyIndex = BodyIndexByBone[BoneIndex];
	const float S = EditorIcons::GetScale();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	TArray<int32> ChildBodies;
	CollectChildBodies(BoneIndex, ChildBodies);
	const bool bCollapsed = CollapsedBones[BoneIndex] != 0 && ChildBodies.Num() > 0;

	// 행 배치: [여백][깊이 × 들여쓰기][아이콘][이름]
	const float RowHeight = ImGui::GetTextLineHeight() + 6.0f * S;
	const ImVec2 RowMin = ImGui::GetCursorScreenPos();
	const ImVec2 IconCenter(RowMin.x + (RowPadding + Depth * IndentWidth + IconSize * 0.5f) * S, RowMin.y + RowHeight * 0.5f);
	const ImVec2 TextPos(IconCenter.x + (IconSize * 0.5f + IconTextGap) * S, RowMin.y + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f);

	char Suffix[32] = "";
	if (bCollapsed)
	{
		snprintf(Suffix, sizeof(Suffix), "  (+%d)", CountBodiesBelow(BoneIndex));
	}
	const float TextEnd = TextPos.x + ImGui::CalcTextSize(Item.Name.c_str()).x + ImGui::CalcTextSize(Suffix).x + RowPadding * S;

	// 행 전체를 덮는 Selectable로 클릭 · 하이라이트를 받는다 (가로 스크롤을 위해 이름 끝까지는 넓힌다)
	const bool bSelected = Editor.GetSelectionType() == ESelection::Body && Editor.GetSelectedIndex() == BodyIndex;
	ImGui::PushID(BoneIndex);
	const float RowWidth = (std::max)(ImGui::GetContentRegionAvail().x, TextEnd - RowMin.x);
	if (ImGui::Selectable("##Body", bSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(RowWidth, RowHeight)))
	{
		Editor.SetSelection(ESelection::Body, BodyIndex);
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ChildBodies.IsEmpty())
		{
			CollapsedBones[BoneIndex] = CollapsedBones[BoneIndex] != 0 ? 0 : 1;
		}
	}
	if (bSelected && bScrollToSelection)
	{
		ImGui::SetScrollHereY(0.5f);
	}
	ImGui::PopID();

	const ImU32 IconColor = bSelected ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text, 0.8f);
	EditorIcons::Draw(DrawList, EEditorIcon::PhysicsBody, IconCenter, IconSize * S, IconColor);
	DrawList->AddText(TextPos, ImGui::GetColorU32(ImGuiCol_Text), Item.Name.c_str());
	if (bCollapsed)
	{
		DrawList->AddText(ImVec2(TextPos.x + ImGui::CalcTextSize(Item.Name.c_str()).x, TextPos.y),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), Suffix);
	}

	if (bCollapsed)
	{
		return IconCenter.y;
	}

	// 자식 행을 그리고 연결선을 잇는다:
	//   부모 아이콘 아래에서 마지막 자식까지 세로선, 자식마다 세로선에서 자식 아이콘까지 가로선
	const ImU32 LineColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f);
	const float LineX = IconCenter.x;
	const float ChildIconLeft = IconCenter.x + (IndentWidth - IconSize * 0.5f - 2.0f) * S;
	float LastChildY = IconCenter.y;
	for (int32 ChildBone : ChildBodies)
	{
		const float ChildY = DrawBody(ChildBone, Depth + 1);
		DrawList->AddLine(ImVec2(LineX, ChildY), ImVec2(ChildIconLeft, ChildY), LineColor);
		LastChildY = ChildY;
	}
	if (!ChildBodies.IsEmpty())
	{
		DrawList->AddLine(ImVec2(LineX, IconCenter.y + IconSize * 0.5f * S), ImVec2(LineX, LastChildY), LineColor);
	}
	return IconCenter.y;
}
