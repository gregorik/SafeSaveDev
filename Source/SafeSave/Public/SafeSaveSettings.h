// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SafeSaveSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "SafeSave"))
class SAFESAVE_API USafeSaveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USafeSaveSettings();

	UPROPERTY(EditAnywhere, config, Category = "Status", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float DirtyCheckIntervalSeconds;

	UPROPERTY(EditAnywhere, config, Category = "Source Control", meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "Status Poll Interval (Seconds)"))
	float GitCheckIntervalSeconds;

	UPROPERTY(EditAnywhere, config, Category = "Source Control", meta = (DisplayName = "Auto Fetch (Git Only)"))
	bool bAutoFetch;

	UPROPERTY(EditAnywhere, config, Category = "Source Control", meta = (ClampMin = "10.0", UIMin = "10.0", EditCondition = "bAutoFetch", DisplayName = "Auto Fetch Interval (Seconds, Git Only)"))
	float AutoFetchIntervalSeconds;

	UPROPERTY(EditAnywhere, config, Category = "Notifications")
	bool bToastOnStatusChange;

	UPROPERTY(EditAnywhere, config, Category = "Notifications", meta = (ClampMin = "0.5", UIMin = "0.5"))
	float StatusToastMinIntervalSeconds;

	virtual FName GetCategoryName() const override
	{
		return FName("Plugins");
	}
};
