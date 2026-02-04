// Copyright Epic Games, Inc. All Rights Reserved.

#include "SafeSaveModule.h"
#include "SafeSaveStyle.h"
#include "SafeSaveSettings.h"
#include "SSafeSaveToolbar.h"

#include "ISettingsModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FSafeSaveModule"

void FSafeSaveModule::StartupModule()
{
	FSafeSaveStyle::Initialize();
	FSafeSaveStyle::ReloadTextures();

	if (UToolMenus::Get())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSafeSaveModule::RegisterMenus)
		);
	}

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Editor",
			"Plugins",
			"SafeSave",
			LOCTEXT("SafeSaveSettingsName", "SafeSave"),
			LOCTEXT("SafeSaveSettingsDescription", "Configure SafeSave source control status and automation settings."),
			GetMutableDefault<USafeSaveSettings>()
		);
	}
}

void FSafeSaveModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "SafeSave");
	}

	FSafeSaveStyle::Shutdown();
}

void FSafeSaveModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");

	if (Menu)
	{
		FToolMenuSection& Section = Menu->FindOrAddSection("SafeSaveControls");
		UToolMenus::Get()->RemoveEntry("LevelEditor.LevelEditorToolBar.User", "SafeSaveControls", "SafeSaveStatusWidget");
		FToolMenuEntry Entry = FToolMenuEntry::InitWidget(
			"SafeSaveStatusWidget",
			SNew(SSafeSaveToolbar),
			LOCTEXT("SafeSaveLabel", "SafeSave"),
			true
		);
		Section.AddEntry(Entry);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSafeSaveModule, SafeSave)
