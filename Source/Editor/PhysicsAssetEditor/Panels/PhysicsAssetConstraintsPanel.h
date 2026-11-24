#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Core/EngineString.h"

class UPhysicsAsset;

// 선택한 바디에 이어진 관절 목록 (UE 그래프 패널의 깊이 1을 표 하나로 줄인 것).
//   Body: spine_03
//   ┌──────────────┬────────┐
//   │ spine_02     │ Parent │   ← spine_03이 자식인 관절 (spine_02 – spine_03)
//   │ neck_01      │ Child  │   ← spine_03이 부모인 관절
//   │ upperarm_l   │ Child  │
//   └──────────────┴────────┘
// 행을 클릭하면 그 관절을 선택하고 (뷰포트 · 디테일 패널이 따라감), 우클릭 메뉴나 Delete 키로 관절만 지운다.
// 관절을 골라도 목록은 바뀌지 않도록 "기준 바디"는 본 이름으로 따로 기억한다 (지우면 인덱스가 밀리기 때문)
class FPhysicsAssetConstraintsPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetConstraintsPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// 선택에 맞춰 기준 바디를 정한다.
	//   바디 선택           → 그 바디
	//   관절 선택 (목록에 있음) → 그대로
	//   관절 선택 (목록에 없음) → 그 관절의 자식 바디 (뷰포트에서 다른 관절을 고른 경우)
	void UpdateFocusBody(const UPhysicsAsset& Asset);

	FString FocusBoneName;
	int32 PendingDeleteConstraint = -1;   // 우클릭 메뉴에서 고른 삭제 (목록을 다 그린 뒤 실행)
};
