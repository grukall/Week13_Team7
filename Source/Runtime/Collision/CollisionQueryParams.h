#pragma once

#include "Physics/BodyInstance.h"

struct FCollisionObjectQueryParams
{
	uint32 ObjectTypesToQuery = 0;

	static bool IsValidObjectQuery(ECollisionChannel Channel)
	{
		return Channel != ECollisionChannel::ECC_Visibility && Channel != ECollisionChannel::ECC_Camera &&
			static_cast<uint32>(Channel) <= static_cast<uint32>(ECollisionChannel::ECC_Destructible);
	}

	void AddObjectTypesToQuery(ECollisionChannel Channel)
	{
		if (IsValidObjectQuery(Channel))
			ObjectTypesToQuery |= 1u << static_cast<uint32>(Channel);
	}
};
