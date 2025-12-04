// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSafeSaveToolbar.h"

// Engine UI & Style
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

// Logic
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h" 
#include "ISourceControlState.h"
#include "FileHelpers.h"             
#include "UObject/UObjectIterator.h" 
#include "UObject/Package.h"

// Execution Framework
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

#define LOCTEXT_NAMESPACE "SafeSaveToolbar"

static FString GlobalDirtyPackageName = TEXT("");

void SSafeSaveToolbar::Construct(const FArguments& InArgs)
{
	bIsDirty = true;
	bIsConnected = true;
	bHasRemoteChanges = true;

	ChildSlot
		[
			SNew(SButton)
				.OnClicked(this, &SSafeSaveToolbar::OnClicked)
				.ContentPadding(FMargin(6.0f, 2.0f))
				.ToolTipText(this, &SSafeSaveToolbar::GetTooltip)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SImage)
								.Image(this, &SSafeSaveToolbar::GetIcon)
								.ColorAndOpacity(this, &SSafeSaveToolbar::GetColor)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(this, &SSafeSaveToolbar::GetLabel)
								.Font(FAppStyle::GetFontStyle("BoldFont"))
								.ShadowOffset(FVector2D(1.0f, 1.0f))
						]
				]
		];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SSafeSaveToolbar::UpdateState));
}

EActiveTimerReturnType SSafeSaveToolbar::UpdateState(double InCurrentTime, float InDeltaTime)
{
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	bIsConnected = SCModule.IsEnabled() && SCModule.GetProvider().IsAvailable();
	ISourceControlProvider& Provider = SCModule.GetProvider();

	// Reset Flags
	bIsDirty = false;
	bHasRemoteChanges = false;
	GlobalDirtyPackageName = TEXT("");

	// Deep Scan
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Pkg = *It;

		if (Pkg == GetTransientPackage()) continue;
		if (Pkg->HasAnyPackageFlags(PKG_CompiledIn)) continue;

		FString PkgName = Pkg->GetName();

		// --- REVISED FILTERING LOGIC --- //

		// 1. Block Engine & Script internals (Always safe to ignore)
		if (PkgName.StartsWith(TEXT("/Engine/"))) continue;
		if (PkgName.StartsWith(TEXT("/Script/"))) continue;
		if (PkgName.StartsWith(TEXT("/Memory/"))) continue;

		// 2. Block specific World Partition "Ghost" Instances
		// This targets the specific issue you saw earlier without blocking all of /Temp/
		if (PkgName.Contains(TEXT("_InstanceOf_"))) continue;

		// 3. Block Autosaves (Optional, but usually user only cares about manual saves)
		if (PkgName.Contains(TEXT("/Autosaves/"))) continue;

		// (Removed the "StartsWith /Temp/" block so "Untitled_1" works again)
		// (Removed the PKG_FilterEditorOnly check so Editor Assets work again)

		// --- CHECK DIRTY --- //
		if (Pkg->IsDirty())
		{
			bIsDirty = true;
			GlobalDirtyPackageName = PkgName;
		}

		// --- CHECK REMOTE --- //
		if (bIsConnected)
		{
			FSourceControlStatePtr State = Provider.GetState(Pkg, EStateCacheUsage::Use);
			if (State.IsValid() && !State->IsCurrent())
			{
				bHasRemoteChanges = true;
			}
		}

		// optimization: break if both flags found
		if (bIsDirty && bHasRemoteChanges) break;
	}

	static double LastFullCheck = 0;
	if (InCurrentTime - LastFullCheck > 5.0f && bIsConnected)
	{
		CheckForUpdates();
		LastFullCheck = InCurrentTime;
	}

	return EActiveTimerReturnType::Continue;
}

void SSafeSaveToolbar::CheckForUpdates()
{
	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
	UpdateOp->SetUpdateHistory(true);
	Provider.Execute(UpdateOp, TArray<FString>(), EConcurrency::Asynchronous);
}

FReply SSafeSaveToolbar::OnClicked()
{
	if (!GUnrealEd) return FReply::Handled();

	if (!bIsConnected)
	{
		GUnrealEd->Exec(nullptr, TEXT("SourceControl.Connect"));
		return FReply::Handled();
	}

	// RED STATE
	if (bIsDirty && bHasRemoteChanges)
	{
		const bool bSaved = FEditorFileUtils::SaveDirtyPackages(true, true, true, false, false, false);
		if (bSaved)
		{
			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TSharedRef<FSync, ESPMode::ThreadSafe> SyncOp = ISourceControlOperation::Create<FSync>();
			Provider.Execute(SyncOp, TArray<FString>(), EConcurrency::Asynchronous);
		}
	}
	// ORANGE STATE
	else if (bIsDirty)
	{
		const bool bSaved = FEditorFileUtils::SaveDirtyPackages(true, true, true, false, false, false);
		if (bSaved)
		{
			GUnrealEd->Exec(nullptr, TEXT("SourceControl.Submit"));
		}
	}
	// BLUE STATE
	else if (bHasRemoteChanges)
	{
		GUnrealEd->Exec(nullptr, TEXT("SourceControl.SyncFile"));
	}
	// GREEN STATE
	else
	{
		CheckForUpdates();
	}

	return FReply::Handled();
}

const FSlateBrush* SSafeSaveToolbar::GetIcon() const
{
	if (bIsDirty) return FAppStyle::GetBrush("Icons.Save");
	if (bHasRemoteChanges) return FAppStyle::GetBrush("Icons.Download");
	if (!bIsConnected) return FAppStyle::GetBrush("Icons.Warning");
	return FAppStyle::GetBrush("Icons.Lock");
}

FText SSafeSaveToolbar::GetLabel() const
{
	if (bIsDirty && bHasRemoteChanges) return LOCTEXT("StatusConflict", "Safe Update");
	if (bIsDirty) return LOCTEXT("StatusSave", "Push Changes");
	if (bHasRemoteChanges) return LOCTEXT("StatusUpdate", "Get Updates");
	if (!bIsConnected) return LOCTEXT("StatusOffline", "Offline");
	return LOCTEXT("StatusSync", "Synced");
}

FSlateColor SSafeSaveToolbar::GetColor() const
{
	if (bIsDirty && bHasRemoteChanges) return FLinearColor(1.0f, 0.1f, 0.1f);
	if (bIsDirty) return FLinearColor(1.0f, 0.4f, 0.0f);
	if (bHasRemoteChanges) return FLinearColor(0.0f, 0.4f, 1.0f);
	if (!bIsConnected) return FLinearColor::Gray;
	return FLinearColor(0.1f, 0.8f, 0.2f);
}

FText SSafeSaveToolbar::GetTooltip() const
{
	if (bIsDirty)
	{
		return FText::Format(LOCTEXT("TipDirtyDebug", "Unsaved changes in:\n{0}\n\nClick to Save & Submit."), FText::FromString(GlobalDirtyPackageName));
	}

	if (bHasRemoteChanges) return LOCTEXT("TipBlue", "New files available on server. Click to download.");
	if (!bIsConnected) return LOCTEXT("TipOffline", "Source Control is disabled. Click to connect.");
	return LOCTEXT("TipGreen", "All good.");
}

#undef LOCTEXT_NAMESPACE