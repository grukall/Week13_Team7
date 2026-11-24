#include "EnginePCH.h"
#include "PhysicsAssetSkeletonTreePanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

#include <cstdio>

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
		// 행 사이 간격을 없앤다. 우클릭 메뉴는 이 값을 물려받지 않도록 원래 간격을 기억해 둔다
		MenuItemSpacing = ImGui::GetStyle().ItemSpacing;
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

	// Delete 키: 이 패널에 포커스가 있을 때 선택한 바디를 지운다 (뷰포트도 같은 키를 받는다)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		Editor.DeleteSelection();
	}

	// 메뉴에서 고른 작업은 트리를 다 그린 뒤에 한다. 그리는 도중에 바디 배열이 바뀌면
	// 남은 행들이 밀린 인덱스로 선택 · 클릭을 처리한다
	if (PendingAddCapsuleBody != -1)
	{
		Editor.AddCapsuleToBody(PendingAddCapsuleBody);
		PendingAddCapsuleBody = -1;
	}
	if (PendingDeleteBody != -1)
	{
		Editor.DeleteBody(PendingDeleteBody);
		PendingDeleteBody = -1;
	}
}

void FPhysicsAssetSkeletonTreePanel::DrawBodyContextMenu(int32 BodyIndex)
{
	const UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const USkeletalBodySetup* Setup = Asset && Asset->SkeletalBodySetups.IsValidIndex(BodyIndex) ? Asset->SkeletalBodySetups[BodyIndex] : nullptr;
	if (!Setup)
	{
		return;
	}

	// 우클릭으로 연 메뉴라 키보드 탐색 표시(파란 테두리)가 마우스와 다른 줄에 남지 않게 숨긴다
	ImGui::SetNavCursorVisible(false);

	// 위쪽 메뉴는 ImGui 기본 MenuItem / BeginMenu로 통일한다 (EditorIcons::MenuRow는 아이콘 칸이 있어 글자 위치가 어긋난다)
	EditorIcons::MenuSection("Body");

	// 도형만 Tool 패널 옵션으로 다시 만든다 (바디 설정 · 관절은 그대로)
	if (ImGui::MenuItem("Regenerate Body"))
	{
		Editor.RegenerateBody(BodyIndex);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Regenerate the selected body's shape with the Tool panel settings.");
	}

	if (ImGui::BeginMenu("Add Primitive"))
	{
		// 지금은 캡슐만 지원한다 (자동 생성 · 기즈모 · 피킹이 캡슐 기준)
		EditorIcons::MenuSection("Primitive Type");
		if (ImGui::MenuItem("Add Capsule"))
		{
			PendingAddCapsuleBody = BodyIndex;
		}
		ImGui::EndMenu();
	}

	// 이 바디(자식)를 고른 바디(부모)에 관절로 잇는다. UE처럼 검색 칸 + 바디 목록
	if (ImGui::BeginMenu("Add Constraint"))
	{
		EditorIcons::MenuSection("Create New Constraint With...");
		if (ImGui::IsWindowAppearing())
		{
			ConstraintSearch[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		EditorIcons::SearchBox("##ConstraintSearch", ConstraintSearch, sizeof(ConstraintSearch), "Search bodies...", 220.0f * EditorIcons::GetScale());

		if (ImGui::BeginChild("##ConstraintTargets", ImVec2(220.0f * EditorIcons::GetScale(), 260.0f * EditorIcons::GetScale())))
		{
			for (int32 Other = 0; Other < Asset->SkeletalBodySetups.Num(); ++Other)
			{
				const USkeletalBodySetup* OtherSetup = Asset->SkeletalBodySetups[Other];
				if (Other == BodyIndex || !OtherSetup || !EditorIcons::MatchesFilter(OtherSetup->BoneName.c_str(), ConstraintSearch))
				{
					continue;
				}
				// 이미 이어진 바디는 흐리게 막는다 (같은 두 바디 사이에 관절을 두 개 만들지 않는다)
				const bool bConnected = Editor.HasConstraintBetween(BodyIndex, Other);
				ImGui::PushID(Other);
				if (EditorIcons::MenuRow(OtherSetup->BoneName.c_str(), EEditorIcon::PhysicsBody, false, !bConnected))
				{
					Editor.AddConstraint(BodyIndex, Other);
					ImGui::CloseCurrentPopup();
				}
				if (bConnected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Already connected");
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::EndMenu();
	}

	// 피직스 타입: 이 바디만, 또는 본 계층에서 이 바디 아래의 모든 바디
	if (ImGui::BeginMenu("Physics Type"))
	{
		static const char* TypeNames[] = { "Default", "Kinematic", "Simulated" };
		EditorIcons::MenuSection("Selected Body");
		for (int32 i = 0; i < 3; ++i)
		{
			const EPhysicsType Type = static_cast<EPhysicsType>(i);
			if (ImGui::MenuItem(TypeNames[i], nullptr, Setup->PhysicsType == Type))
			{
				Editor.SetBodyPhysicsType(BodyIndex, Type, false);
			}
		}

		EditorIcons::MenuSection("All Bodies Below");
		for (int32 i = 0; i < 3; ++i)
		{
			char Label[48];
			std::snprintf(Label, sizeof(Label), "Set All Bodies Below to %s", TypeNames[i]);
			if (ImGui::MenuItem(Label))
			{
				Editor.SetBodyPhysicsType(BodyIndex, static_cast<EPhysicsType>(i), true);
			}
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// 바디를 지우면 그 바디에 이어진 관절도 함께 지워진다
	if (ImGui::MenuItem("Delete", "Delete"))
	{
		PendingDeleteBody = BodyIndex;
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Delete the selected body and the constraints connected to it.");
	}
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

	// 바디 선택만 따라간다 (관절은 Constraints 패널에서 보여 준다)
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

	// 우클릭: 그 바디를 고르고 메뉴를 띄운다 (UE 스켈레톤 트리 메뉴의 "바디" 섹션 중 일부)
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		Editor.SetSelection(ESelection::Body, BodyIndex);
	}
	// 트리가 행 간격을 0으로 바꿔 둬서, 메뉴는 원래 간격으로 되돌려 연다 (안 하면 메뉴 줄이 붙어 보인다)
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, MenuItemSpacing);
	if (ImGui::BeginPopupContextItem("BodyContextMenu"))
	{
		DrawBodyContextMenu(BodyIndex);
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
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
