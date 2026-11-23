#include "EnginePCH.h"
#include "PhysicsConstraintTemplate.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/MathSerialization.h"

namespace
{
	// enum은 이름으로 저장한다. 항목 순서를 바꿔도 파일이 깨지지 않는다
	void SerializeMotion(FStructuredArchive::FRecord& Record, FArchiveFieldName Name, EAngularMotion& Motion)
	{
		static const char* Names[] = { "Free", "Limited", "Locked" };
		FString Value = Names[static_cast<int32>(Motion)];
		Record << MakeNamedValue(Name, Value);
		if (!Record.GetUnderlyingArchive().IsLoading())
			return;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Value == Names[i])
			{
				Motion = static_cast<EAngularMotion>(i);
				return;
			}
		}
		HTR_LOG(Warning, "PhysicsAsset: unknown angular motion '{}', keeping default", Value);
	}

	// 위치 + 쿼터니언. 조인트 프레임에 스케일은 의미가 없으므로 저장하지 않는다
	void SerializeFrame(FStructuredArchive::FRecord Record, FTransform& Frame)
	{
		Record << SA_VALUE("Location", Frame.Location);
		SerializeQuat(Record.EnterRecord("Rotation"), Frame.Rotation);
		if (Record.GetUnderlyingArchive().IsLoading())
			Frame.Scale = FVector::OneVector;
	}
}

void UPhysicsConstraintTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FConstraintInstance& CI = DefaultInstance;

	Record << SA_VALUE("Bone1", CI.ConstraintBone1) << SA_VALUE("Bone2", CI.ConstraintBone2);
	SerializeFrame(Record.EnterRecord("Frame1"), CI.Frame1);
	SerializeFrame(Record.EnterRecord("Frame2"), CI.Frame2);

	SerializeMotion(Record, "TwistMotion", CI.TwistMotion);
	SerializeMotion(Record, "Swing1Motion", CI.Swing1Motion);
	SerializeMotion(Record, "Swing2Motion", CI.Swing2Motion);
	Record << SA_VALUE("TwistLimitDeg", CI.TwistLimitDeg)
	       << SA_VALUE("Swing1LimitDeg", CI.Swing1LimitDeg)
	       << SA_VALUE("Swing2LimitDeg", CI.Swing2LimitDeg);
}
