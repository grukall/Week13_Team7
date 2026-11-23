#pragma once

#include "PhysicsAssetEditorPanel.h"

// 프리뷰 렌더 결과(렌더 타깃)를 표시하고, 패널 크기·카메라·마우스 입력을 에디터에 넘긴다
class FPhysicsAssetViewportPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetViewportPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;
};
