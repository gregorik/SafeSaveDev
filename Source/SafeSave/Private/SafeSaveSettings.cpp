// Copyright Epic Games, Inc. All Rights Reserved.

#include "SafeSaveSettings.h"

USafeSaveSettings::USafeSaveSettings()
{
	DirtyCheckIntervalSeconds = 1.0f;
	GitCheckIntervalSeconds = 5.0f;
	bAutoFetch = false;
	AutoFetchIntervalSeconds = 120.0f;
	bToastOnStatusChange = true;
	StatusToastMinIntervalSeconds = 4.0f;
}
