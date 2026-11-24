#pragma once

#include "Editor/Details/PropertyEditor.h"
#include "Components/ClothComponent.h"

// 천 컴포넌트 설정 구조체의 디테일 패널 표시 규칙. 꺼진 기능의 세부 값은 숨겨 필요한 것만 보이게 한다.
// 런타임(ClothComponent)은 에디터를 모르므로 규칙은 에디터 쪽에서 등록한다 (ParticleSystemEditor의 RegisterDistributionDetails와 같은 방식).
// FDetailsPanel::Init에서 한 번 부른다. 헤더 전용이라 프로젝트 재생성 없이 빌드된다
namespace ClothDetails
{
	inline void RegisterStructMemberRules()
	{
		static bool bRegistered = false;
		if (bRegistered) return;
		bRegistered = true;

		// 바람을 끄면 세기를 숨긴다
		FPropertyEditor::RegisterStructMemberRule(FClothSimSettings::StaticStruct(),
			[](const void* Value, const FProperty& Member) -> FPropertyEditor::FStructMemberDisplay
			{
				const FClothSimSettings& Settings = *static_cast<const FClothSimSettings*>(Value);
				const bool bVisible = Member.Name != "WindStrength" || Settings.bEnableWind;
				return { bVisible, nullptr };
			});

		// 자기 충돌을 끄면 거리·강성을 숨긴다
		FPropertyEditor::RegisterStructMemberRule(FClothSelfCollisionSettings::StaticStruct(),
			[](const void* Value, const FProperty& Member) -> FPropertyEditor::FStructMemberDisplay
			{
				const FClothSelfCollisionSettings& Settings = *static_cast<const FClothSelfCollisionSettings*>(Value);
				const bool bVisible = Member.Name == "bEnabled" || Settings.bEnabled;
				return { bVisible, Member.Name == "bEnabled" ? "Enabled" : nullptr };
			});
	}
}
