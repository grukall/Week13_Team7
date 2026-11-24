#include "EnginePCH.h"
#include "PhysicsAssetDetailsPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/Details/PropertyEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

#include <cstdio>

namespace
{
	using ESelection = FPhysicsAssetEditor::EPhysicsSelection;
	using EFrameComponent = FPhysicsAssetEditor::EConstraintFrameComponent;

	constexpr float LabelColumnRatio = 0.45f;      // 라벨 | 값 비율 (DetailsPanel과 같다)
	constexpr float MinCapsuleSize = 0.005f;       // 뷰포트 스케일 기즈모와 같은 최솟값

	// 뷰포트와 같은 색: 스윙 콘은 빨강, 트위스트 부채꼴은 초록
	const ImVec4 SwingColor(1.0f, 0.3f, 0.25f, 1.0f);
	const ImVec4 TwistColor(0.35f, 1.0f, 0.35f, 1.0f);

	// 접히는 카테고리 헤더 (기본 펼침)
	bool Category(const char* Name)
	{
		return ImGui::CollapsingHeader(Name, ImGuiTreeNodeFlags_DefaultOpen);
	}

	// 직접 그리는 한 줄: 라벨을 쓰고 값 칸 시작으로 옮긴다. RightReserve만큼 오른쪽(↶ 버튼 자리)을 남긴다
	void BeginRow(const char* Label, float RightReserve = 0.0f)
	{
		const float StartX = ImGui::GetCursorPosX();
		const float Available = ImGui::GetContentRegionAvail().x;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::SameLine();
		ImGui::SetCursorPosX(StartX + Available * LabelColumnRatio);
		ImGui::SetNextItemWidth(RightReserve > 0.0f ? -RightReserve : -1.0f);
	}

	// x · y · z 칸 세 개 (왼쪽에 축 색 막대). 전체 폭은 SetNextItemWidth를 따른다
	bool DrawXYZ(const char* Id, float* Values, float Speed, const char* Format)
	{
		static const ImU32 AxisColors[3] = { IM_COL32(214, 64, 64, 255), IM_COL32(110, 186, 52, 255), IM_COL32(60, 120, 230, 255) };
		const float Spacing = 4.0f * EditorIcons::GetScale();
		const float BarWidth = 3.0f * EditorIcons::GetScale();
		const float FieldWidth = ImMax(1.0f, (ImGui::CalcItemWidth() - Spacing * 2.0f) / 3.0f);

		bool bChanged = false;
		ImGui::PushID(Id);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Spacing, ImGui::GetStyle().ItemSpacing.y));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis > 0)
			{
				ImGui::SameLine();
			}
			ImGui::PushID(Axis);
			ImGui::SetNextItemWidth(FieldWidth);
			bChanged |= ImGui::DragFloat("##Axis", &Values[Axis], Speed, 0.0f, 0.0f, Format);
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRectFilled(Min, ImVec2(Min.x + BarWidth, Max.y), AxisColors[Axis],
				ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
		ImGui::PopID();
		return bChanged;
	}

	// 회전 한 줄 (X = Roll, Y = Pitch, Z = Yaw, 도). 캐시로 쿼터니언 ↔ 오일러를 바꾼다
	bool DrawRotation(const char* Id, FQuat& Rotation, const FRotationConversionCache& Cache)
	{
		const FRotator& Euler = Cache.QuatToRotator(Rotation);
		float Values[3] = { Euler.Roll, Euler.Pitch, Euler.Yaw };
		if (!DrawXYZ(Id, Values, 0.5f, "%.2f"))
		{
			return false;
		}
		FRotator NewEuler = Euler;
		NewEuler.Roll = Values[0];
		NewEuler.Pitch = Values[1];
		NewEuler.Yaw = Values[2];
		Rotation = Cache.RotatorToQuat(NewEuler);
		return true;
	}

	// ↶ 버튼 폭 (줄 오른쪽에 항상 비워 둬서 버튼이 생겨도 칸 폭이 흔들리지 않게)
	float ResetButtonWidth()
	{
		return EditorIcons::CalcButtonWidth(true, nullptr, false, ImGui::GetFrameHeight());
	}

	// 리플렉션 속성 한 줄 (UObject 멤버)
	bool DrawObjectProperty(UObject& Object, const char* PropertyName, const char* Label)
	{
		const FProperty* Property = Object.GetClass()->FindPropertyByName(PropertyName);
		return Property && FPropertyEditor::DrawProperty(&Object, *Property, nullptr, Label);
	}

	// 리플렉션 속성 한 줄 (USTRUCT 멤버). Owner의 OwnerPropertyName 구조체 안의 MemberName
	bool DrawStructProperty(UObject& Owner, const char* OwnerPropertyName, void* StructValue, const UStruct* StructType,
		const char* MemberName, const char* Label)
	{
		const FProperty* OwnerProperty = Owner.GetClass()->FindPropertyByName(OwnerPropertyName);
		const FProperty* Member = StructType ? StructType->FindPropertyByName(MemberName) : nullptr;
		return OwnerProperty && Member && FPropertyEditor::DrawStructMemberProperty(&Owner, *OwnerProperty, StructValue, *Member, nullptr, Label);
	}

	// Free / Limited / Locked 라디오 한 줄. 선택 점은 뷰포트의 제한 색으로 칠한다
	bool DrawMotionRadio(const char* Label, EAngularMotion& Motion, const ImVec4& Color)
	{
		static const char* Names[] = { "Free", "Limited", "Locked" };
		BeginRow(Label);
		bool bChanged = false;
		ImGui::PushID(Label);
		ImGui::PushStyleColor(ImGuiCol_CheckMark, Color);
		for (int32 i = 0; i < 3; ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine();
			}
			if (ImGui::RadioButton(Names[i], static_cast<int32>(Motion) == i) && static_cast<int32>(Motion) != i)
			{
				Motion = static_cast<EAngularMotion>(i);
				bChanged = true;
			}
		}
		ImGui::PopStyleColor();
		ImGui::PopID();
		return bChanged;
	}
}

FPhysicsAssetDetailsPanel::FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Details")
{
}

void FPhysicsAssetDetailsPanel::DrawContent()
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	if (!Editor.GetMesh() || !Asset)
	{
		return;
	}

	const int32 Index = Editor.GetSelectedIndex();
	switch (Editor.GetSelectionType())
	{
	case ESelection::Body:
		if (Asset->SkeletalBodySetups.IsValidIndex(Index) && Asset->SkeletalBodySetups[Index])
		{
			DrawBodyDetails(*Asset->SkeletalBodySetups[Index]);
			return;
		}
		break;
	case ESelection::Constraint:
		if (Asset->ConstraintSetup.IsValidIndex(Index) && Asset->ConstraintSetup[Index])
		{
			DrawConstraintDetails(*Asset->ConstraintSetup[Index], Index);
			return;
		}
		break;
	default:
		break;
	}
	DrawAssetSummary(*Asset);
}

void FPhysicsAssetDetailsPanel::DrawAssetSummary(const UPhysicsAsset& Asset)
{
	ImGui::TextDisabled("Select a body or constraint to edit it.");
	ImGui::Separator();
	ImGui::Text("Bodies: %d", Asset.SkeletalBodySetups.Num());
	ImGui::Text("Constraints: %d", Asset.ConstraintSetup.Num());
	ImGui::Spacing();
	ImGui::TextDisabled("Asset");
	ImGui::TextWrapped("%s", Editor.GetAssetPath().c_str());
	if (Editor.IsDirty())
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Unsaved changes");
	}
}

// ─────────────────────────────────────────────────────────────
// Body
// ─────────────────────────────────────────────────────────────

void FPhysicsAssetDetailsPanel::DrawBodyDetails(USkeletalBodySetup& Setup)
{
	bool bChanged = false;
	ImGui::PushID(&Setup);

	if (Category("Body Setup"))
	{
		bChanged |= DrawObjectProperty(Setup, "BoneName", "Bone Name");
		bChanged |= DrawObjectProperty(Setup, "PhysicsType", "Physics Type");
	}

	if (Category("Physics"))
	{
		bChanged |= DrawObjectProperty(Setup, "bOverrideMass", "Override Mass");
		// 지정하지 않으면 도형 부피 × 밀도로 정해지니 값 칸을 잠근다
		ImGui::BeginDisabled(!Setup.bOverrideMass);
		bChanged |= DrawObjectProperty(Setup, "MassInKg", "Mass (kg)");
		ImGui::EndDisabled();
		bChanged |= DrawObjectProperty(Setup, "LinearDamping", "Linear Damping");
		bChanged |= DrawObjectProperty(Setup, "AngularDamping", "Angular Damping");
		bChanged |= DrawObjectProperty(Setup, "bEnableGravity", "Enable Gravity");
		bChanged |= DrawObjectProperty(Setup, "PhysMaterial", "Phys Material");
		bChanged |= DrawObjectProperty(Setup, "COMNudge", "Center of Mass Offset");
		bChanged |= DrawObjectProperty(Setup, "bOverrideMaxAngularVelocity", "Override Max Angular Velocity");
		ImGui::BeginDisabled(!Setup.bOverrideMaxAngularVelocity);
		bChanged |= DrawObjectProperty(Setup, "MaxAngularVelocity", "Max Angular Velocity (deg/s)");
		ImGui::EndDisabled();
	}

	if (Category("Collision"))
	{
		bChanged |= DrawObjectProperty(Setup, "CollisionEnabled", "Collision Enabled");
		bChanged |= DrawObjectProperty(Setup, "bUseCCD", "Use CCD");
	}

	if (Category("Primitives"))
	{
		TArray<FKSphylElem>& Capsules = Setup.AggGeom.SphylElems;
		if (Capsules.IsEmpty())
		{
			ImGui::TextDisabled("No capsules");
		}
		if (CapsuleRotations.Num() < Capsules.Num())
		{
			CapsuleRotations.SetNum(Capsules.Num());
		}
		for (int32 i = 0; i < Capsules.Num(); ++i)
		{
			bChanged |= DrawCapsule(Capsules[i], i);
		}
	}

	ImGui::PopID();
	if (bChanged)
	{
		Editor.MarkDirty();
	}
}

bool FPhysicsAssetDetailsPanel::DrawCapsule(FKSphylElem& Capsule, int32 CapsuleIndex)
{
	char Header[32];
	std::snprintf(Header, sizeof(Header), "Capsule %d", CapsuleIndex);
	ImGui::PushID(CapsuleIndex);
	bool bChanged = false;
	if (ImGui::TreeNodeEx(Header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
	{
		// 본 공간 값. 길이 축은 캡슐 로컬 Z
		BeginRow("Center");
		bChanged |= DrawXYZ("Center", Capsule.Center.V, 0.001f, "%.3f");

		BeginRow("Rotation");
		bChanged |= DrawRotation("Rotation", Capsule.Rotation, CapsuleRotations[CapsuleIndex]);

		BeginRow("Radius");
		if (ImGui::DragFloat("##Radius", &Capsule.Radius, 0.001f, MinCapsuleSize, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp))
		{
			bChanged = true;
		}

		// 반구 두 개를 뺀 원기둥 길이. 0이면 구
		BeginRow("Length");
		if (ImGui::DragFloat("##Length", &Capsule.Length, 0.001f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp))
		{
			bChanged = true;
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	return bChanged;
}

// ─────────────────────────────────────────────────────────────
// Constraint
// ─────────────────────────────────────────────────────────────

void FPhysicsAssetDetailsPanel::DrawConstraintDetails(UPhysicsConstraintTemplate& Template, int32 ConstraintIndex)
{
	FConstraintInstance& CI = Template.DefaultInstance;
	const UStruct* InstanceStruct = FConstraintInstance::StaticStruct();
	bool bChanged = false;
	ImGui::PushID(&Template);

	if (Category("Constraint"))
	{
		// UE처럼 관절 이름 = 자식 본 이름 (우리 데이터엔 따로 저장하지 않는다)
		BeginRow("Joint Name");
		char JointName[128];
		std::snprintf(JointName, sizeof(JointName), "%s", CI.ConstraintBone1.c_str());
		ImGui::BeginDisabled();
		ImGui::InputText("##JointName", JointName, sizeof(JointName), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();

		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "ConstraintBone1", "Child Bone");
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "ConstraintBone2", "Parent Bone");
	}

	if (Category("Constraint Transforms"))
	{
		// 두 프레임을 따로 고친다 (UE와 같음). 둘이 어긋나면 시뮬레이션 시작 때 관절이 튀니, ↶로 기본값에 맞출 수 있다
		bChanged |= DrawConstraintFrame(CI.Frame1, ConstraintIndex, true);
		bChanged |= DrawConstraintFrame(CI.Frame2, ConstraintIndex, false);
	}

	if (Category("Angular Limits"))
	{
		bChanged |= DrawMotionRadio("Swing 1 Motion", CI.Swing1Motion, SwingColor);
		bChanged |= DrawMotionRadio("Swing 2 Motion", CI.Swing2Motion, SwingColor);
		bChanged |= DrawMotionRadio("Twist Motion", CI.TwistMotion, TwistColor);

		// 제한 각도는 Limited일 때만 쓰인다. 다른 모드에서는 값을 남겨 두고 흐리게만 보인다 (UE와 같음)
		const auto DrawLimit = [&](const char* Member, const char* Label, EAngularMotion Motion)
		{
			ImGui::BeginDisabled(Motion != EAngularMotion::Limited);
			bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, Member, Label);
			ImGui::EndDisabled();
		};
		DrawLimit("Swing1LimitDeg", "Swing 1 Limit", CI.Swing1Motion);
		DrawLimit("Swing2LimitDeg", "Swing 2 Limit", CI.Swing2Motion);
		DrawLimit("TwistLimitDeg", "Twist Limit", CI.TwistMotion);

		// Soft Limit은 Limited 축에만 의미가 있다. 켜야 Stiffness · Damping 칸이 열린다
		const auto DrawSoft = [&](const char* Toggle, const char* ToggleLabel, bool bSoft, bool bAnyLimited,
			const char* Stiffness, const char* StiffnessLabel, const char* Damping, const char* DampingLabel)
		{
			ImGui::BeginDisabled(!bAnyLimited);
			bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, Toggle, ToggleLabel);
			ImGui::BeginDisabled(!bSoft);
			bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, Stiffness, StiffnessLabel);
			bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, Damping, DampingLabel);
			ImGui::EndDisabled();
			ImGui::EndDisabled();
		};
		DrawSoft("bSoftSwingLimit", "Soft Swing Limit", CI.bSoftSwingLimit,
			CI.Swing1Motion == EAngularMotion::Limited || CI.Swing2Motion == EAngularMotion::Limited,
			"SwingStiffness", "Swing Stiffness", "SwingDamping", "Swing Damping");
		DrawSoft("bSoftTwistLimit", "Soft Twist Limit", CI.bSoftTwistLimit,
			CI.TwistMotion == EAngularMotion::Limited,
			"TwistStiffness", "Twist Stiffness", "TwistDamping", "Twist Damping");
	}

	if (Category("Constraint Behavior"))
	{
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "bDisableCollision", "Disable Collision");
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "bParentDominates", "Parent Dominates");
	}

	if (Category("Projection"))
	{
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "bEnableProjection", "Enable Projection");
		ImGui::BeginDisabled(!CI.bEnableProjection);
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "ProjectionLinearTolerance", "Linear Tolerance (m)");
		bChanged |= DrawStructProperty(Template, "DefaultInstance", &CI, InstanceStruct, "ProjectionAngularToleranceDeg", "Angular Tolerance (deg)");
		ImGui::EndDisabled();
	}

	ImGui::PopID();
	if (bChanged)
	{
		Editor.MarkDirty();
	}
}

bool FPhysicsAssetDetailsPanel::DrawConstraintFrame(FTransform& Frame, int32 ConstraintIndex, bool bChild)
{
	const EFrameComponent PositionComponent = bChild ? EFrameComponent::ChildPosition : EFrameComponent::ParentPosition;
	const EFrameComponent RotationComponent = bChild ? EFrameComponent::ChildRotation : EFrameComponent::ParentRotation;
	const FRotationConversionCache& RotationCache = bChild ? ChildFrameRotation : ParentFrameRotation;
	const float ResetWidth = ResetButtonWidth();
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;

	ImGui::PushID(bChild ? "Child" : "Parent");

	// 소제목: 왼쪽 프레임 이름, 오른쪽 기즈모 단축키 힌트 (UE와 같은 표시)
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(bChild ? "Child" : "Parent");
	const char* Shortcut = bChild ? "[Shift+Alt]" : "[Alt]";
	ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(Shortcut).x);
	ImGui::TextDisabled("%s", Shortcut);

	// ↶: 그 성분이 기본값(자동 생성 값)과 다를 때만 보인다. 자리는 항상 비워 둔다
	const auto DrawSnapButton = [&](const char* Id, EFrameComponent Component)
	{
		ImGui::SameLine(0.0f, Spacing);
		if (Editor.IsConstraintFrameAtDefault(ConstraintIndex, Component))
		{
			ImGui::Dummy(ImVec2(ResetWidth, ImGui::GetFrameHeight()));
			return false;
		}
		if (EditorIcons::Button(Id, EEditorIcon::Reset, ImGui::GetColorU32(ImGuiCol_Text, 0.75f), nullptr, false, false, ImGui::GetFrameHeight()))
		{
			return Editor.SnapConstraintFrameToDefault(ConstraintIndex, Component);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Snap to default (auto-generated) value");
		}
		return false;
	};

	bool bChanged = false;
	ImGui::Indent();

	// 위치 · 회전은 그 본 공간 값 (Child = 자식 본, Parent = 부모 본)
	BeginRow("Position", ResetWidth + Spacing);
	bChanged |= DrawXYZ("Position", Frame.Location.V, 0.001f, "%.3f");
	// 스냅은 에디터가 직접 고치고 MarkDirty까지 하니 bChanged에 더하지 않아도 된다
	DrawSnapButton("##SnapPosition", PositionComponent);

	BeginRow("Rotation", ResetWidth + Spacing);
	bChanged |= DrawRotation("Rotation", Frame.Rotation, RotationCache);
	DrawSnapButton("##SnapRotation", RotationComponent);

	ImGui::Unindent();
	ImGui::PopID();
	return bChanged;
}
