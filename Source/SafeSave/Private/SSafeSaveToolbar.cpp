// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSafeSaveToolbar.h"

#include "SafeSaveSettings.h"

#include "Async/Async.h"
#include "Editor/UnrealEdEngine.h"
#include "FileHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "ISourceControlModule.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Styling/AppStyle.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SafeSaveToolbar"

namespace
{
	FString TrimCopy(const FString& InText)
	{
		FString OutText = InText;
		OutText.TrimStartAndEndInline();
		return OutText;
	}

	const FString PlasticFieldSeparator = TEXT("|");
	const FString PlasticLineStart = TEXT("@@SAFE@@");
	const FString PlasticLineEnd = TEXT("##SAFE##");

	bool IsPlasticAuthError(const FString& Text)
	{
		const FString Lower = Text.ToLower();
		return Lower.Contains(TEXT("login"))
			|| Lower.Contains(TEXT("log in"))
			|| Lower.Contains(TEXT("authentication"))
			|| Lower.Contains(TEXT("credential"))
			|| Lower.Contains(TEXT("unauthorized"))
			|| Lower.Contains(TEXT("not authorized"))
			|| Lower.Contains(TEXT("access denied"))
			|| Lower.Contains(TEXT("token"))
			|| Lower.Contains(TEXT("expired"));
	}
}

void SSafeSaveToolbar::Construct(const FArguments& InArgs)
{
	bHasUnsavedAssets = false;
	UnsavedAssetCount = 0;
	SampleUnsavedPackage.Reset();
	SourceControlStatus = FSourceControlStatus();
	bStatusUpdateInFlight = false;
	LastAutoFetchSeconds = FPlatformTime::Seconds();
	LastStatusLabel.Reset();
	LastStatusToastSeconds = 0.0;
	bHasSeenStatusLabel = false;

	ChildSlot
	[
		SNew(SComboButton)
			.OnGetMenuContent(this, &SSafeSaveToolbar::BuildMenu)
			.ContentPadding(FMargin(6.0f, 2.0f))
			.ToolTipText(this, &SSafeSaveToolbar::GetTooltip)
			.ButtonContent()
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
	UpdateUnsavedState();
	RequestSourceControlStatusUpdate();
}

EActiveTimerReturnType SSafeSaveToolbar::UpdateState(double InCurrentTime, float InDeltaTime)
{
	const double NowSeconds = FPlatformTime::Seconds();
	const USafeSaveSettings* Settings = GetDefault<USafeSaveSettings>();
	const double DirtyInterval = Settings ? FMath::Max(0.1, (double)Settings->DirtyCheckIntervalSeconds) : 1.0;
	const double GitInterval = Settings ? FMath::Max(1.0, (double)Settings->GitCheckIntervalSeconds) : 5.0;

	if (NowSeconds - LastDirtyCheckSeconds >= DirtyInterval)
	{
		UpdateUnsavedState();
		LastDirtyCheckSeconds = NowSeconds;
	}

	if (NowSeconds - LastSourceControlCheckSeconds >= GitInterval)
	{
		RequestSourceControlStatusUpdate();
		LastSourceControlCheckSeconds = NowSeconds;
	}

	if (Settings && Settings->bAutoFetch && IsGitProvider())
	{
		const double AutoFetchInterval = FMath::Max(10.0, (double)Settings->AutoFetchIntervalSeconds);
		const FSourceControlStatus Status = GetStatusSnapshot();
		const bool bCanAutoFetch = Status.bClientAvailable && Status.bRepo && !bStatusUpdateInFlight.Load();

		if (bCanAutoFetch && (NowSeconds - LastAutoFetchSeconds >= AutoFetchInterval))
		{
			RunGitCommandAsync(TEXT("fetch --prune"), LOCTEXT("AutoFetchSuccess", "Auto fetch completed."), LOCTEXT("AutoFetchFail", "Auto fetch failed."), true, true);
			LastAutoFetchSeconds = NowSeconds;
		}
	}

	return EActiveTimerReturnType::Continue;
}

void SSafeSaveToolbar::UpdateUnsavedState()
{
	TArray<UPackage*> DirtyPackages;
	FEditorFileUtils::GetDirtyPackages(DirtyPackages);

	bHasUnsavedAssets = DirtyPackages.Num() > 0;
	UnsavedAssetCount = DirtyPackages.Num();
	SampleUnsavedPackage = bHasUnsavedAssets ? DirtyPackages[0]->GetName() : FString();
	MaybeNotifyStatusChange();
}

void SSafeSaveToolbar::RequestSourceControlStatusUpdate()
{
	if (bStatusUpdateInFlight.Load())
	{
		return;
	}

	StartSourceControlStatusUpdate();
}

void SSafeSaveToolbar::StartSourceControlStatusUpdate()
{
	bStatusUpdateInFlight = true;

	TWeakPtr<SSafeSaveToolbar> SelfWeak = StaticCastSharedRef<SSafeSaveToolbar>(AsShared());
	Async(EAsyncExecution::ThreadPool, [SelfWeak]()
	{
		TSharedPtr<SSafeSaveToolbar> Pinned = SelfWeak.Pin();
		if (!Pinned.IsValid())
		{
			return;
		}

		FSourceControlStatus NewStatus;
		NewStatus.LastUpdateUtc = FDateTime::UtcNow();

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString GitError;
	FString PlasticError;
	bool bGitClientAvailable = false;
	bool bPlasticClientAvailable = false;

	const ESourceControlProvider PreferredProvider = Pinned->GetPreferredProvider();
	FSourceControlStatus GitStatus;
	FSourceControlStatus PlasticStatus;
	bool bGitRepoFound = false;
	bool bPlasticRepoFound = false;

	if (PreferredProvider == ESourceControlProvider::Plastic)
	{
		bPlasticRepoFound = Pinned->TryPopulatePlasticStatus(ProjectDir, PlasticStatus, PlasticError);
		NewStatus = PlasticStatus;
	}
	else if (PreferredProvider == ESourceControlProvider::Git)
	{
		bGitRepoFound = Pinned->TryPopulateGitStatus(ProjectDir, GitStatus, GitError);
		NewStatus = GitStatus;
	}
	else
	{
		bGitRepoFound = Pinned->TryPopulateGitStatus(ProjectDir, GitStatus, GitError);
		bGitClientAvailable = GitStatus.bClientAvailable;

		if (!bGitRepoFound)
		{
			bPlasticRepoFound = Pinned->TryPopulatePlasticStatus(ProjectDir, PlasticStatus, PlasticError);
			bPlasticClientAvailable = PlasticStatus.bClientAvailable;
		}

		if (bGitRepoFound)
		{
			NewStatus = GitStatus;
		}
		else if (bPlasticRepoFound)
		{
			NewStatus = PlasticStatus;
		}
		else
		{
			NewStatus = FSourceControlStatus();
			NewStatus.Provider = ESourceControlProvider::None;
			NewStatus.bClientAvailable = bGitClientAvailable || bPlasticClientAvailable;
			NewStatus.bRepo = false;

			TArray<FString> Errors;
			if (!GitError.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("Git: %s"), *GitError));
			}
			if (!PlasticError.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("Plastic SCM: %s"), *PlasticError));
			}
			NewStatus.LastError = Errors.Num() > 0 ? FString::Join(Errors, TEXT("\n")) : FString();
		}
	}

		AsyncTask(ENamedThreads::GameThread, [SelfWeak, NewStatus]()
		{
			TSharedPtr<SSafeSaveToolbar> PinnedGame = SelfWeak.Pin();
			if (!PinnedGame.IsValid())
			{
				return;
			}

			PinnedGame->SourceControlStatus = NewStatus;
			PinnedGame->bStatusUpdateInFlight = false;
			PinnedGame->MaybeNotifyStatusChange();
		});
	});
}

bool SSafeSaveToolbar::TryPopulateGitStatus(const FString& ProjectDir, FSourceControlStatus& OutStatus, FString& OutError) const
{
	OutStatus = FSourceControlStatus();
	OutStatus.Provider = ESourceControlProvider::Git;

	FString StdOut;
	FString StdErr;
	int32 ExitCode = 0;

	const bool bGitLaunched = RunGit(TEXT("rev-parse --show-toplevel"), ProjectDir, StdOut, StdErr, ExitCode);
	if (!bGitLaunched)
	{
		OutStatus.bClientAvailable = false;
		OutError = TEXT("Git executable not found.");
		OutStatus.LastError = OutError;
		return false;
	}

	OutStatus.bClientAvailable = true;

	if (ExitCode != 0)
	{
		OutStatus.bRepo = false;
		OutError = TrimCopy(StdErr);
		OutStatus.LastError = OutError;
		return false;
	}

	OutStatus.bRepo = true;
	OutStatus.RepoRoot = TrimCopy(StdOut);

	StdOut.Reset();
	StdErr.Reset();
	ExitCode = 0;

	const bool bStatusOk = RunGit(TEXT("status --porcelain=v2 -b"), OutStatus.RepoRoot, StdOut, StdErr, ExitCode);
	if (bStatusOk && ExitCode == 0)
	{
		ParseGitStatusOutput(StdOut, OutStatus);
	}
	else
	{
		OutStatus.LastError = TrimCopy(StdErr);
		OutError = OutStatus.LastError;
	}

	return true;
}

bool SSafeSaveToolbar::TryPopulatePlasticStatus(const FString& ProjectDir, FSourceControlStatus& OutStatus, FString& OutError) const
{
	OutStatus = FSourceControlStatus();
	OutStatus.Provider = ESourceControlProvider::Plastic;

	FString StdOut;
	FString StdErr;
	int32 ExitCode = 0;

	const FString WorkspaceArgs = FString::Printf(TEXT("getworkspacefrompath \"%s\" --format=\"{wkname}|{wkpath}\""), *ProjectDir);
	const bool bPlasticLaunched = RunPlastic(WorkspaceArgs, ProjectDir, StdOut, StdErr, ExitCode);

	if (!bPlasticLaunched)
	{
		OutStatus.bClientAvailable = false;
		OutError = TEXT("Plastic SCM CLI not found.");
		OutStatus.LastError = OutError;
		return false;
	}

	OutStatus.bClientAvailable = true;

	if (ExitCode != 0 || StdOut.IsEmpty())
	{
		OutStatus.bRepo = false;
		const FString Combined = TrimCopy(StdErr + TEXT("\n") + StdOut);
		if (IsPlasticAuthError(Combined))
		{
			OutStatus.bAuthRequired = true;
			OutError = TEXT("Plastic SCM login required.");
			OutStatus.LastError = Combined;
		}
		else
		{
			OutError = TrimCopy(StdErr);
			OutStatus.LastError = OutError;
		}
		return false;
	}

	FString WorkspaceName;
	FString WorkspaceRoot;
	{
		const FString Trimmed = TrimCopy(StdOut);
		TArray<FString> Parts;
		Trimmed.ParseIntoArray(Parts, TEXT("|"), true);
		if (Parts.Num() >= 2)
		{
			WorkspaceName = TrimCopy(Parts[0]);
			WorkspaceRoot = TrimCopy(Parts[1]);
		}
	}

	if (WorkspaceRoot.IsEmpty())
	{
		OutStatus.bRepo = false;
		OutError = TEXT("Plastic SCM workspace root not found.");
		OutStatus.LastError = OutError;
		return false;
	}

	OutStatus.bRepo = true;
	OutStatus.RepoRoot = WorkspaceRoot;
	OutStatus.WorkspaceName = WorkspaceName;

	StdOut.Reset();
	StdErr.Reset();
	ExitCode = 0;

	const FString WorkspaceInfoArgs = FString::Printf(TEXT("workspaceinfo \"%s\""), *OutStatus.RepoRoot);
	const bool bInfoOk = RunPlastic(WorkspaceInfoArgs, OutStatus.RepoRoot, StdOut, StdErr, ExitCode);
	if (bInfoOk && ExitCode == 0)
	{
		TArray<FString> InfoLines;
		StdOut.ParseIntoArrayLines(InfoLines, true);
		for (const FString& Line : InfoLines)
		{
			const FString Trimmed = TrimCopy(Line);
			int32 SplitIndex = INDEX_NONE;
			if (Trimmed.StartsWith(TEXT("Branch")) && (Trimmed.FindChar(TEXT(':'), SplitIndex) || Trimmed.FindChar(TEXT('='), SplitIndex)))
			{
				OutStatus.Branch = TrimCopy(Trimmed.Mid(SplitIndex + 1));
				break;
			}
		}
	}
	else
	{
		const FString Combined = TrimCopy(StdErr + TEXT("\n") + StdOut);
		if (IsPlasticAuthError(Combined))
		{
			OutStatus.bAuthRequired = true;
			OutStatus.LastError = Combined;
			OutError = TEXT("Plastic SCM login required.");
			return true;
		}
	}

	StdOut.Reset();
	StdErr.Reset();
	ExitCode = 0;

	const bool bHeaderOk = RunPlastic(TEXT("status --header --head"), OutStatus.RepoRoot, StdOut, StdErr, ExitCode);
	if (bHeaderOk && ExitCode == 0)
	{
		int32 CurrentChangeset = INDEX_NONE;
		int32 HeadChangeset = INDEX_NONE;

		const FRegexPattern CsPattern(TEXT("cs:(\\d+)"));
		const FRegexPattern HeadPattern(TEXT("head:(\\d+)"));

		TArray<FString> HeaderLines;
		StdOut.ParseIntoArrayLines(HeaderLines, true);

		for (const FString& Line : HeaderLines)
		{
			FRegexMatcher CsMatcher(CsPattern, Line);
			if (CsMatcher.FindNext())
			{
				CurrentChangeset = FCString::Atoi(*CsMatcher.GetCaptureGroup(1));
			}

			FRegexMatcher HeadMatcher(HeadPattern, Line);
			if (HeadMatcher.FindNext())
			{
				HeadChangeset = FCString::Atoi(*HeadMatcher.GetCaptureGroup(1));
			}

			if (OutStatus.Branch.IsEmpty())
			{
				FString Left = Line;
				int32 ParenIndex = INDEX_NONE;
				if (Left.FindChar(TEXT('('), ParenIndex))
				{
					Left = Left.Left(ParenIndex);
				}
				Left = TrimCopy(Left);

				if (Left.StartsWith(TEXT("/")) || Left.StartsWith(TEXT("lb:"), ESearchCase::IgnoreCase))
				{
					int32 AtIndex = Left.Find(TEXT("@"));
					if (AtIndex != INDEX_NONE)
					{
						Left = Left.Left(AtIndex);
					}
					OutStatus.Branch = TrimCopy(Left);
				}
			}
		}

		if (CurrentChangeset >= 0 && HeadChangeset >= 0)
		{
			OutStatus.bHasUpstream = true;
			OutStatus.Behind = FMath::Max(0, HeadChangeset - CurrentChangeset);
			OutStatus.Ahead = FMath::Max(0, CurrentChangeset - HeadChangeset);
		}
	}
	else
	{
		const FString Combined = TrimCopy(StdErr + TEXT("\n") + StdOut);
		if (IsPlasticAuthError(Combined))
		{
			OutStatus.bAuthRequired = true;
			OutStatus.LastError = Combined;
			OutError = TEXT("Plastic SCM login required.");
			return true;
		}
	}

	StdOut.Reset();
	StdErr.Reset();
	ExitCode = 0;

	const FString StatusArgs = FString::Printf(
		TEXT("status --machinereadable --noheader --controlledchanged --private --fieldseparator=%s --startlineseparator=%s --endlineseparator=%s"),
		*PlasticFieldSeparator,
		*PlasticLineStart,
		*PlasticLineEnd
	);

	const bool bStatusOk = RunPlastic(StatusArgs, OutStatus.RepoRoot, StdOut, StdErr, ExitCode);
	if (bStatusOk && ExitCode == 0)
	{
		ParsePlasticStatusOutput(StdOut, OutStatus);
	}
	else
	{
		const FString Combined = TrimCopy(StdErr + TEXT("\n") + StdOut);
		if (IsPlasticAuthError(Combined))
		{
			OutStatus.bAuthRequired = true;
			OutStatus.LastError = Combined;
			OutError = TEXT("Plastic SCM login required.");
		}
		else
		{
			OutStatus.LastError = TrimCopy(StdErr);
			OutError = OutStatus.LastError;
		}
	}

	return true;
}

void SSafeSaveToolbar::ParseGitStatusOutput(const FString& Output, FSourceControlStatus& Status) const
{
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines, true);

	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("# branch.head ")))
		{
			Status.Branch = TrimCopy(Line.RightChop(14));
		}
		else if (Line.StartsWith(TEXT("# branch.upstream ")))
		{
			Status.bHasUpstream = true;
		}
		else if (Line.StartsWith(TEXT("# branch.ab ")))
		{
			const FString Ab = TrimCopy(Line.RightChop(12));
			TArray<FString> Parts;
			Ab.ParseIntoArray(Parts, TEXT(" "), true);

			for (const FString& Part : Parts)
			{
				if (Part.StartsWith(TEXT("+")))
				{
					Status.Ahead = FCString::Atoi(*Part.RightChop(1));
				}
				else if (Part.StartsWith(TEXT("-")))
				{
					Status.Behind = FCString::Atoi(*Part.RightChop(1));
				}
			}
		}
		else if (Line.StartsWith(TEXT("1 ")) || Line.StartsWith(TEXT("2 ")))
		{
			if (Line.Len() > 3)
			{
				const TCHAR X = Line[2];
				const TCHAR Y = Line[3];

				if (X != TEXT('.'))
				{
					Status.Staged++;
				}
				if (Y != TEXT('.'))
				{
					Status.Unstaged++;
				}
				if (X == TEXT('U') || Y == TEXT('U'))
				{
					Status.bHasConflicts = true;
				}
			}
		}
		else if (Line.StartsWith(TEXT("u ")))
		{
			Status.bHasConflicts = true;
		}
		else if (Line.StartsWith(TEXT("? ")))
		{
			Status.Untracked++;
		}
	}
}

void SSafeSaveToolbar::ParsePlasticStatusOutput(const FString& Output, FSourceControlStatus& Status) const
{
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines, true);

	int32 ChangeCount = 0;
	int32 UntrackedCount = 0;
	bool bHasConflicts = false;

	for (const FString& Line : Lines)
	{
		FString CleanLine = Line;
		CleanLine.ReplaceInline(*PlasticLineStart, TEXT(""));
		CleanLine.ReplaceInline(*PlasticLineEnd, TEXT(""));
		CleanLine = TrimCopy(CleanLine);

		if (CleanLine.IsEmpty())
		{
			continue;
		}

		TArray<FString> Fields;
		CleanLine.ParseIntoArray(Fields, *PlasticFieldSeparator, false);
		if (Fields.Num() == 0)
		{
			continue;
		}

		const FString Code = TrimCopy(Fields[0]);
		if (Code.Equals(TEXT("STATUS"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		ChangeCount++;

		TArray<FString> CodeParts;
		Code.ParseIntoArray(CodeParts, TEXT("+"), true);
		for (const FString& Part : CodeParts)
		{
			if (Part.Equals(TEXT("PR"), ESearchCase::IgnoreCase))
			{
				UntrackedCount++;
				break;
			}
		}

		for (const FString& Field : Fields)
		{
			const FString UpperField = Field.ToUpper();
			if (UpperField.Contains(TEXT("CONFLICT")))
			{
				bHasConflicts = true;
				break;
			}
			if (UpperField.Contains(TEXT("MERGE")) && !UpperField.Contains(TEXT("NO_MERGES")))
			{
				bHasConflicts = true;
				break;
			}
		}
	}

	Status.Untracked = UntrackedCount;
	Status.Unstaged = FMath::Max(0, ChangeCount - UntrackedCount);
	Status.bHasConflicts = bHasConflicts;
}

SSafeSaveToolbar::FSourceControlStatus SSafeSaveToolbar::GetStatusSnapshot() const
{
	return SourceControlStatus;
}

SSafeSaveToolbar::ESourceControlProvider SSafeSaveToolbar::GetPreferredProvider() const
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		const FString ProviderName = ISourceControlModule::Get().GetProvider().GetName().ToString().ToLower();
		if (ProviderName.Contains(TEXT("plastic")) || ProviderName.Contains(TEXT("unity")))
		{
			return ESourceControlProvider::Plastic;
		}
		if (ProviderName.Contains(TEXT("git")))
		{
			return ESourceControlProvider::Git;
		}
	}

	return ESourceControlProvider::None;
}

TSharedRef<SWidget> SSafeSaveToolbar::BuildMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("SafeSaveActions", LOCTEXT("SafeSaveActions", "SafeSave"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SaveAll", "Save All"),
		LOCTEXT("SaveAllTooltip", "Save all dirty assets and maps."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
		FUIAction(FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteSaveAll))
	);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("Refresh", "Refresh Status"),
		LOCTEXT("RefreshTooltip", "Re-scan dirty assets and refresh source control status."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteRefresh))
	);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowStatus", "Show Status Details"),
		LOCTEXT("ShowStatusTooltip", "Show a detailed SafeSave status summary."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
		FUIAction(FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteShowStatus))
	);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("SourceControlActions", GetProviderLabel());
	if (IsGitProvider())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("GitFetch", "Fetch"),
			LOCTEXT("GitFetchTooltip", "Fetch latest refs from remote without changing local files."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteGitFetch),
				FCanExecuteAction::CreateSP(this, &SSafeSaveToolbar::CanExecuteGitCommand)
			)
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("AutoFetchToggle", "Auto Fetch"),
			LOCTEXT("AutoFetchToggleTooltip", "Automatically fetch from remote at a configurable interval."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ToggleAutoFetch),
				FCanExecuteAction::CreateSP(this, &SSafeSaveToolbar::CanExecuteGitCommand),
				FIsActionChecked::CreateSP(this, &SSafeSaveToolbar::IsAutoFetchEnabled)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
		MenuBuilder.AddWidget(
			SNew(SBox)
			.Padding(FMargin(16.0f, 4.0f))
			[
				SNew(STextBlock)
					.Text(this, &SSafeSaveToolbar::GetAutoFetchIntervalLabel)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
			],
			FText::GetEmpty(),
			true
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("GitPull", "Pull (Rebase)"),
			LOCTEXT("GitPullTooltip", "Pull from upstream using rebase. Only enabled when the working tree is clean."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteGitPullRebase),
				FCanExecuteAction::CreateSP(this, &SSafeSaveToolbar::CanExecuteGitPull)
			)
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("GitPush", "Push"),
			LOCTEXT("GitPushTooltip", "Push local commits to upstream. Only enabled when the working tree is clean."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecuteGitPush),
				FCanExecuteAction::CreateSP(this, &SSafeSaveToolbar::CanExecuteGitPush)
			)
		);
	}
	else if (IsPlasticProvider())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("PlasticUpdate", "Update Workspace"),
			LOCTEXT("PlasticUpdateTooltip", "Update workspace to the latest changeset. Only enabled when the working tree is clean."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSafeSaveToolbar::ExecutePlasticUpdate),
				FCanExecuteAction::CreateSP(this, &SSafeSaveToolbar::CanExecutePlasticUpdate)
			)
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SSafeSaveToolbar::ExecuteSaveAll()
{
	FEditorFileUtils::SaveDirtyPackages(true, true, true, false, false, false);
	UpdateUnsavedState();
}

void SSafeSaveToolbar::ExecuteRefresh()
{
	UpdateUnsavedState();
	RequestSourceControlStatusUpdate();
}

void SSafeSaveToolbar::ExecuteGitFetch()
{
	RunGitCommandAsync(TEXT("fetch --prune"), LOCTEXT("FetchSuccess", "Fetch completed."), LOCTEXT("FetchFail", "Fetch failed."), true);
}

void SSafeSaveToolbar::ExecuteGitPullRebase()
{
	if (!CanExecuteGitPull())
	{
		Notify(LOCTEXT("PullDisabled", "Pull is disabled until the working tree is clean and upstream is set."), false);
		return;
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("ConfirmPull", "Pull from upstream with rebase? This will update your working tree.")
	);

	if (Result == EAppReturnType::Yes)
	{
		RunGitCommandAsync(TEXT("pull --rebase"), LOCTEXT("PullSuccess", "Pull completed."), LOCTEXT("PullFail", "Pull failed."), true);
	}
}

void SSafeSaveToolbar::ExecuteGitPush()
{
	if (!CanExecuteGitPush())
	{
		Notify(LOCTEXT("PushDisabled", "Push is disabled until the working tree is clean, ahead, and upstream is set."), false);
		return;
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("ConfirmPush", "Push local commits to upstream?")
	);

	if (Result == EAppReturnType::Yes)
	{
		RunGitCommandAsync(TEXT("push"), LOCTEXT("PushSuccess", "Push completed."), LOCTEXT("PushFail", "Push failed."), true);
	}
}

void SSafeSaveToolbar::ExecutePlasticUpdate()
{
	if (!CanExecutePlasticUpdate())
	{
		Notify(LOCTEXT("PlasticUpdateDisabled", "Update is disabled until the workspace is clean and there are no unsaved assets."), false);
		return;
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("ConfirmPlasticUpdate", "Update workspace to the latest changeset?")
	);

	if (Result == EAppReturnType::Yes)
	{
		RunPlasticCommandAsync(TEXT("update"), LOCTEXT("PlasticUpdateSuccess", "Update completed."), LOCTEXT("PlasticUpdateFail", "Update failed."), true);
	}
}

void SSafeSaveToolbar::ExecuteShowStatus()
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	const FString Summary = BuildStatusSummary(Status);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
}

void SSafeSaveToolbar::ToggleAutoFetch()
{
	USafeSaveSettings* Settings = GetMutableDefault<USafeSaveSettings>();
	if (!Settings)
	{
		return;
	}

	Settings->bAutoFetch = !Settings->bAutoFetch;
	Settings->SaveConfig();
	LastAutoFetchSeconds = FPlatformTime::Seconds();
}

bool SSafeSaveToolbar::CanExecuteGitCommand() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	return IsGitProvider() && Status.bClientAvailable && Status.bRepo;
}

bool SSafeSaveToolbar::CanExecuteGitPull() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	const bool bCleanTree = (Status.Staged + Status.Unstaged + Status.Untracked) == 0;
	return IsGitProvider() && Status.bClientAvailable && Status.bRepo && Status.bHasUpstream && Status.Behind > 0 && bCleanTree && !bHasUnsavedAssets;
}

bool SSafeSaveToolbar::CanExecuteGitPush() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	const bool bCleanTree = (Status.Staged + Status.Unstaged + Status.Untracked) == 0;
	return IsGitProvider() && Status.bClientAvailable && Status.bRepo && Status.bHasUpstream && Status.Ahead > 0 && Status.Behind == 0 && bCleanTree && !bHasUnsavedAssets;
}

bool SSafeSaveToolbar::CanExecutePlasticUpdate() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	const bool bCleanTree = (Status.Staged + Status.Unstaged + Status.Untracked) == 0;
	return IsPlasticProvider() && Status.bClientAvailable && Status.bRepo && bCleanTree && !bHasUnsavedAssets;
}

bool SSafeSaveToolbar::IsAutoFetchEnabled() const
{
	const USafeSaveSettings* Settings = GetDefault<USafeSaveSettings>();
	return Settings && Settings->bAutoFetch;
}

bool SSafeSaveToolbar::IsGitProvider() const
{
	return GetStatusSnapshot().Provider == ESourceControlProvider::Git;
}

bool SSafeSaveToolbar::IsPlasticProvider() const
{
	return GetStatusSnapshot().Provider == ESourceControlProvider::Plastic;
}

const FSlateBrush* SSafeSaveToolbar::GetIcon() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();

	if (Status.bAuthRequired)
	{
		return FAppStyle::GetBrush("Icons.WarningWithColor");
	}

	if (!Status.bClientAvailable || !Status.bRepo)
	{
		return FAppStyle::GetBrush("Icons.Warning");
	}

	if (Status.bHasConflicts || (Status.Ahead > 0 && Status.Behind > 0))
	{
		return FAppStyle::GetBrush("Icons.WarningWithColor");
	}

	if (bHasUnsavedAssets)
	{
		return FAppStyle::GetBrush("Icons.Save");
	}

	if (Status.Behind > 0)
	{
		return FAppStyle::GetBrush("Icons.Refresh");
	}

	if (Status.Ahead > 0)
	{
		return FAppStyle::GetBrush("Icons.Save");
	}

	if (Status.Staged + Status.Unstaged + Status.Untracked > 0)
	{
		return FAppStyle::GetBrush("Icons.Save");
	}

	return FAppStyle::GetBrush("Icons.Info");
}

FText SSafeSaveToolbar::GetLabel() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();

	if (!Status.bClientAvailable)
	{
		return LOCTEXT("SCMMissing", "SCM Missing");
	}

	if (Status.bAuthRequired)
	{
		return LOCTEXT("SCMLoginRequired", "Login Required");
	}

	if (!Status.bRepo)
	{
		return LOCTEXT("NoRepo", "No SCM Repo");
	}

	FString Branch = Status.Branch;
	if (Branch.IsEmpty())
	{
		Branch = Status.WorkspaceName;
	}
	if (Branch.IsEmpty() && Status.bAuthRequired && IsPlasticProvider())
	{
		Branch = TEXT("Plastic");
	}
	if (Branch.IsEmpty())
	{
		Branch = TEXT("unknown");
	}
	if (IsGitProvider() && Branch.Contains(TEXT("detached")))
	{
		Branch = TEXT("detached");
	}

	FString StateText;
	if (Status.bHasConflicts)
	{
		StateText = TEXT("Conflicts");
	}
	else if (bHasUnsavedAssets)
	{
		StateText = FString::Printf(TEXT("Unsaved %d"), UnsavedAssetCount);
	}
	else if (Status.Ahead > 0 && Status.Behind > 0)
	{
		StateText = TEXT("Diverged");
	}
	else if (Status.Behind > 0)
	{
		StateText = FString::Printf(TEXT("Behind %d"), Status.Behind);
	}
	else if (Status.Staged + Status.Unstaged + Status.Untracked > 0)
	{
		StateText = TEXT("Changes");
	}
	else if (Status.Ahead > 0)
	{
		StateText = FString::Printf(TEXT("Ahead %d"), Status.Ahead);
	}
	else
	{
		StateText = TEXT("Clean");
	}

	return FText::FromString(FString::Printf(TEXT("%s | %s"), *Branch, *StateText));
}

FSlateColor SSafeSaveToolbar::GetColor() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();

	if (Status.bAuthRequired)
	{
		return FLinearColor(1.0f, 0.65f, 0.0f);
	}

	if (!Status.bClientAvailable || !Status.bRepo)
	{
		return FLinearColor::Gray;
	}

	if (Status.bHasConflicts || (Status.Ahead > 0 && Status.Behind > 0))
	{
		return FLinearColor(1.0f, 0.2f, 0.2f);
	}

	if (bHasUnsavedAssets)
	{
		return FLinearColor(1.0f, 0.5f, 0.0f);
	}

	if (Status.Behind > 0)
	{
		return FLinearColor(0.0f, 0.45f, 1.0f);
	}

	if (Status.Staged + Status.Unstaged + Status.Untracked > 0)
	{
		return FLinearColor(1.0f, 0.5f, 0.0f);
	}

	return FLinearColor(0.2f, 0.85f, 0.2f);
}

FText SSafeSaveToolbar::GetTooltip() const
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	FString Tooltip;

	if (!Status.bClientAvailable)
	{
		Tooltip = TEXT("Git or Plastic SCM CLI not found. Install Git or Unity Version Control (Plastic SCM) CLI and restart the editor.");
		if (!Status.LastError.IsEmpty())
		{
			Tooltip += TEXT("\n");
			Tooltip += Status.LastError;
		}
		return FText::FromString(Tooltip);
	}

	if (Status.bAuthRequired)
	{
		Tooltip = TEXT("Plastic SCM login required. Sign in via Source Control to continue.");
		if (!Status.LastError.IsEmpty())
		{
			Tooltip += TEXT("\n");
			Tooltip += Status.LastError;
		}
		return FText::FromString(Tooltip);
	}

	if (!Status.bRepo)
	{
		Tooltip = TEXT("Project is not inside a Git repository or Plastic SCM workspace.");
		if (!Status.LastError.IsEmpty())
		{
			Tooltip += TEXT("\n");
			Tooltip += Status.LastError;
		}
		return FText::FromString(Tooltip);
	}

	Tooltip += FString::Printf(TEXT("Provider: %s\n"), *GetProviderLabel().ToString());
	if (IsPlasticProvider() && !Status.WorkspaceName.IsEmpty())
	{
		Tooltip += FString::Printf(TEXT("Workspace: %s\n"), *Status.WorkspaceName);
	}
	Tooltip += FString::Printf(TEXT("Root: %s\n"), *Status.RepoRoot);

	FString BranchLabel = Status.Branch;
	if (BranchLabel.IsEmpty())
	{
		BranchLabel = Status.WorkspaceName;
	}
	if (!BranchLabel.IsEmpty())
	{
		Tooltip += FString::Printf(TEXT("Branch: %s\n"), *BranchLabel);
	}

	if (IsGitProvider())
	{
		if (Status.bHasUpstream)
		{
			Tooltip += FString::Printf(TEXT("Ahead: %d  Behind: %d\n"), Status.Ahead, Status.Behind);
		}
		else
		{
			Tooltip += TEXT("Upstream: not set\n");
		}

		Tooltip += FString::Printf(TEXT("Staged: %d  Unstaged: %d  Untracked: %d\n"), Status.Staged, Status.Unstaged, Status.Untracked);
	}
	else if (IsPlasticProvider())
	{
		if (Status.Behind > 0)
		{
			Tooltip += FString::Printf(TEXT("Updates available: %d\n"), Status.Behind);
		}
		Tooltip += FString::Printf(TEXT("Pending changes: %d\n"), Status.Unstaged + Status.Untracked);
	}

	if (bHasUnsavedAssets)
	{
		Tooltip += FString::Printf(TEXT("Unsaved assets: %d\n"), UnsavedAssetCount);
		if (!SampleUnsavedPackage.IsEmpty())
		{
			Tooltip += FString::Printf(TEXT("Example: %s\n"), *SampleUnsavedPackage);
		}
	}

	if (Status.LastUpdateUtc != FDateTime())
	{
		const FTimespan Age = FDateTime::UtcNow() - Status.LastUpdateUtc;
		Tooltip += FString::Printf(TEXT("Updated: %ds ago"), (int32)Age.GetTotalSeconds());
	}

	return FText::FromString(Tooltip);
}

FText SSafeSaveToolbar::GetAutoFetchIntervalLabel() const
{
	const USafeSaveSettings* Settings = GetDefault<USafeSaveSettings>();
	const int32 IntervalSeconds = Settings ? FMath::Max(10, (int32)Settings->AutoFetchIntervalSeconds) : 120;
	const bool bEnabled = Settings && Settings->bAutoFetch;

	if (bEnabled)
	{
		return FText::FromString(FString::Printf(TEXT("Auto fetch interval: %ds"), IntervalSeconds));
	}

	return FText::FromString(FString::Printf(TEXT("Auto fetch interval: %ds (disabled)"), IntervalSeconds));
}

FText SSafeSaveToolbar::GetProviderLabel() const
{
	switch (GetStatusSnapshot().Provider)
	{
	case ESourceControlProvider::Git:
		return LOCTEXT("ProviderGit", "Git");
	case ESourceControlProvider::Plastic:
		return LOCTEXT("ProviderPlastic", "Plastic SCM");
	default:
		return LOCTEXT("ProviderSourceControl", "Source Control");
	}
}

void SSafeSaveToolbar::MaybeNotifyStatusChange()
{
	const USafeSaveSettings* Settings = GetDefault<USafeSaveSettings>();
	const FString CurrentLabel = GetLabel().ToString();

	if (!Settings || !Settings->bToastOnStatusChange)
	{
		LastStatusLabel = CurrentLabel;
		bHasSeenStatusLabel = true;
		return;
	}

	if (!bHasSeenStatusLabel)
	{
		LastStatusLabel = CurrentLabel;
		bHasSeenStatusLabel = true;
		return;
	}

	if (CurrentLabel != LastStatusLabel)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		const double MinInterval = FMath::Max(0.5, (double)Settings->StatusToastMinIntervalSeconds);

		if (NowSeconds - LastStatusToastSeconds >= MinInterval)
		{
			Notify(FText::FromString(FString::Printf(TEXT("SafeSave: %s"), *CurrentLabel)), true);
			LastStatusToastSeconds = NowSeconds;
		}

		LastStatusLabel = CurrentLabel;
	}
}

FString SSafeSaveToolbar::BuildStatusSummary(const FSourceControlStatus& Status) const
{
	FString Summary;

	if (!Status.bClientAvailable)
	{
		Summary = TEXT("Git or Plastic SCM CLI not found. Install Git or Unity Version Control (Plastic SCM) CLI and restart the editor.");
		if (!Status.LastError.IsEmpty())
		{
			Summary += TEXT("\n\nDetails:\n");
			Summary += Status.LastError;
		}
		return Summary;
	}

	if (Status.bAuthRequired)
	{
		Summary = TEXT("Plastic SCM login required. Sign in via Source Control to continue.");
		if (!Status.LastError.IsEmpty())
		{
			Summary += TEXT("\n\nDetails:\n");
			Summary += Status.LastError;
		}
		return Summary;
	}

	if (!Status.bRepo)
	{
		Summary = TEXT("Project is not inside a Git repository or Plastic SCM workspace.");
		if (!Status.LastError.IsEmpty())
		{
			Summary += TEXT("\n\nDetails:\n");
			Summary += Status.LastError;
		}
		return Summary;
	}

	Summary += FString::Printf(TEXT("Provider: %s\n"), *GetProviderLabel().ToString());
	if (IsPlasticProvider() && !Status.WorkspaceName.IsEmpty())
	{
		Summary += FString::Printf(TEXT("Workspace: %s\n"), *Status.WorkspaceName);
	}
	Summary += FString::Printf(TEXT("Root: %s\n"), *Status.RepoRoot);

	FString BranchLabel = Status.Branch;
	if (BranchLabel.IsEmpty())
	{
		BranchLabel = Status.WorkspaceName;
	}
	if (!BranchLabel.IsEmpty())
	{
		Summary += FString::Printf(TEXT("Branch: %s\n"), *BranchLabel);
	}

	if (IsGitProvider())
	{
		if (Status.bHasUpstream)
		{
			Summary += FString::Printf(TEXT("Ahead: %d  Behind: %d\n"), Status.Ahead, Status.Behind);
		}
		else
		{
			Summary += TEXT("Upstream: not set\n");
		}

		Summary += FString::Printf(TEXT("Staged: %d  Unstaged: %d  Untracked: %d\n"), Status.Staged, Status.Unstaged, Status.Untracked);
	}
	else if (IsPlasticProvider())
	{
		if (Status.Behind > 0)
		{
			Summary += FString::Printf(TEXT("Updates available: %d\n"), Status.Behind);
		}
		Summary += FString::Printf(TEXT("Pending changes: %d\n"), Status.Unstaged + Status.Untracked);
	}

	if (bHasUnsavedAssets)
	{
		Summary += FString::Printf(TEXT("Unsaved assets: %d\n"), UnsavedAssetCount);
		if (!SampleUnsavedPackage.IsEmpty())
		{
			Summary += FString::Printf(TEXT("Example: %s\n"), *SampleUnsavedPackage);
		}
	}

	return Summary;
}

bool SSafeSaveToolbar::RunGit(const FString& Args, const FString& WorkingDir, FString& OutStdOut, FString& OutStdErr, int32& OutExitCode) const
{
	const FString GitExe = GetGitExecutable();
	return FPlatformProcess::ExecProcess(*GitExe, *Args, &OutExitCode, &OutStdOut, &OutStdErr, *WorkingDir);
}

FString SSafeSaveToolbar::GetGitExecutable() const
{
#if PLATFORM_WINDOWS
	return TEXT("git.exe");
#else
	return TEXT("git");
#endif
}

bool SSafeSaveToolbar::RunPlastic(const FString& Args, const FString& WorkingDir, FString& OutStdOut, FString& OutStdErr, int32& OutExitCode) const
{
	const FString PlasticExe = GetPlasticExecutable();
	return FPlatformProcess::ExecProcess(*PlasticExe, *Args, &OutExitCode, &OutStdOut, &OutStdErr, *WorkingDir);
}

FString SSafeSaveToolbar::GetPlasticExecutable() const
{
#if PLATFORM_WINDOWS
	return TEXT("cm.exe");
#else
	return TEXT("cm");
#endif
}

void SSafeSaveToolbar::RunGitCommandAsync(const FString& Args, const FText& SuccessMessage, const FText& FailureMessage, bool bRefreshAfter, bool bSilentSuccess)
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	if (!IsGitProvider() || !Status.bClientAvailable || !Status.bRepo)
	{
		Notify(LOCTEXT("GitUnavailable", "Git is not available for this project."), false);
		return;
	}

	const FString WorkingDir = Status.RepoRoot.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : Status.RepoRoot;
	TWeakPtr<SSafeSaveToolbar> SelfWeak = StaticCastSharedRef<SSafeSaveToolbar>(AsShared());

	Async(EAsyncExecution::ThreadPool, [SelfWeak, WorkingDir, Args, SuccessMessage, FailureMessage, bRefreshAfter, bSilentSuccess]()
	{
		TSharedPtr<SSafeSaveToolbar> Pinned = SelfWeak.Pin();
		if (!Pinned.IsValid())
		{
			return;
		}

		FString StdOut;
		FString StdErr;
		int32 ExitCode = 0;
		const bool bLaunched = Pinned->RunGit(Args, WorkingDir, StdOut, StdErr, ExitCode);
		const bool bSuccess = bLaunched && ExitCode == 0;
		const FString ErrorText = TrimCopy(StdErr);

		AsyncTask(ENamedThreads::GameThread, [SelfWeak, bSuccess, SuccessMessage, FailureMessage, bRefreshAfter, ErrorText, bSilentSuccess]()
		{
			TSharedPtr<SSafeSaveToolbar> PinnedGame = SelfWeak.Pin();
			if (!PinnedGame.IsValid())
			{
				return;
			}

			if (!(bSuccess && bSilentSuccess))
			{
				PinnedGame->Notify(bSuccess ? SuccessMessage : FailureMessage, bSuccess);
			}
			if (!bSuccess && !ErrorText.IsEmpty())
			{
				PinnedGame->Notify(FText::FromString(ErrorText.Left(200)), false);
			}

			if (bRefreshAfter)
			{
				PinnedGame->RequestSourceControlStatusUpdate();
			}
		});
	});
}

void SSafeSaveToolbar::RunPlasticCommandAsync(const FString& Args, const FText& SuccessMessage, const FText& FailureMessage, bool bRefreshAfter, bool bSilentSuccess)
{
	const FSourceControlStatus Status = GetStatusSnapshot();
	if (!IsPlasticProvider() || !Status.bClientAvailable || !Status.bRepo)
	{
		Notify(LOCTEXT("PlasticUnavailable", "Plastic SCM is not available for this project."), false);
		return;
	}

	const FString WorkingDir = Status.RepoRoot.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : Status.RepoRoot;
	TWeakPtr<SSafeSaveToolbar> SelfWeak = StaticCastSharedRef<SSafeSaveToolbar>(AsShared());

	Async(EAsyncExecution::ThreadPool, [SelfWeak, WorkingDir, Args, SuccessMessage, FailureMessage, bRefreshAfter, bSilentSuccess]()
	{
		TSharedPtr<SSafeSaveToolbar> Pinned = SelfWeak.Pin();
		if (!Pinned.IsValid())
		{
			return;
		}

		FString StdOut;
		FString StdErr;
		int32 ExitCode = 0;
		const bool bLaunched = Pinned->RunPlastic(Args, WorkingDir, StdOut, StdErr, ExitCode);
		const bool bSuccess = bLaunched && ExitCode == 0;
		const FString ErrorText = TrimCopy(StdErr);

		AsyncTask(ENamedThreads::GameThread, [SelfWeak, bSuccess, SuccessMessage, FailureMessage, bRefreshAfter, ErrorText, bSilentSuccess]()
		{
			TSharedPtr<SSafeSaveToolbar> PinnedGame = SelfWeak.Pin();
			if (!PinnedGame.IsValid())
			{
				return;
			}

			if (!(bSuccess && bSilentSuccess))
			{
				PinnedGame->Notify(bSuccess ? SuccessMessage : FailureMessage, bSuccess);
			}
			if (!bSuccess && !ErrorText.IsEmpty())
			{
				PinnedGame->Notify(FText::FromString(ErrorText.Left(200)), false);
			}

			if (bRefreshAfter)
			{
				PinnedGame->RequestSourceControlStatusUpdate();
			}
		});
	});
}

void SSafeSaveToolbar::Notify(const FText& Message, bool bSuccess) const
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 4.0f;
	Info.bUseLargeFont = false;
	Info.bFireAndForget = true;
	Info.Image = FAppStyle::GetBrush(bSuccess ? "Icons.Info" : "Icons.WarningWithColor");

	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
