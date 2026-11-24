#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Containers/Array.h"

class UPhysicsAsset;

// 바디 계층 트리. 바디가 있는 본만 행이 되고, 부모는 "가장 가까운 조상 바디"다 (관절이 잇는 관계와 같다).
//   ⊘ pelvis
//   ├ ⊘ spine_02
//   │  └ ⊘ spine_03 ...
//   └ ⊘ thigh_l ...
// UE처럼 펼침 화살표 대신 바디 아이콘과 연결선을 그린다. 접기/펼치기는 더블클릭.
// 관절은 여기 넣지 않고 그래프 패널에서 보여 준다.
// 행을 클릭하면 뷰포트와 같은 선택(SetSelection)이 되고, 뷰포트에서 바디를 고르면 조상이 펼쳐지며 그 행으로 스크롤된다
class FPhysicsAssetSkeletonTreePanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// Bone 아래에서 가장 가까운 바디 본들 (바디 없는 본은 건너뛰고 그 자식을 본다)
	void CollectChildBodies(int32 BoneIndex, TArray<int32>& OutBodyBones) const;
	int32 CountBodiesBelow(int32 BoneIndex) const;

	// 바디 행 하나와 그 자식 행들을 그린다. 연결선용으로 이 행 아이콘의 중심 Y를 돌려준다
	float DrawBody(int32 BoneIndex, int32 Depth);

	// 선택이 바뀐 프레임에 선택한 바디의 조상을 펼치고 스크롤을 예약한다
	void UpdateSelectionTracking(const UPhysicsAsset& Asset);

	// 본 인덱스로 찾는 표. TArray<bool>은 std::vector<bool> 특수화라 bool&를 못 돌려줘 uint8을 쓴다
	TArray<int32> BodyIndexByBone;          // 프레임마다 다시 만든다 (-1 = 바디 없음)
	TArray<uint8> CollapsedBones;           // 유지되는 접힘 상태 (1 = 접힘)

	// 선택 추적
	int32 LastSelectionType = -1;
	int32 LastSelectedIndex = -1;
	bool bScrollToSelection = false;
};
