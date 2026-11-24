#pragma once

#include "PhysicsAssetEditorPanel.h"

// 바디 · 관절 자동 생성 옵션 (UE Physics Asset Editor의 Tool 탭).
// 값은 에디터의 FPhysAssetCreateParams에 있고, 툴바의 Regenerate Bodies가 이 값으로 다시 만든다
class FPhysicsAssetToolPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetToolPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;
};
