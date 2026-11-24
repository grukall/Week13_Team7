#pragma once

#include "PhysicsAssetEditorPanel.h"

// 프리뷰 뷰포트 설정 (레벨 에디터 Settings 패널의 카메라 항목과 같은 역할).
// 값은 에디터(FSkeletalPreviewEditor)가 들고 있고 이 에디터 탭에만 적용된다
class FPhysicsAssetSettingsPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetSettingsPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;
};
