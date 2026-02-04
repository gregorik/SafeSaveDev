// Copyright Epic Games, Inc. All Rights Reserved.

#include "SafeSaveStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h" 

// Definition of the singleton
TSharedPtr<FSlateStyleSet> FSafeSaveStyle::StyleInstance = nullptr;

void FSafeSaveStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FSafeSaveStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

void FSafeSaveStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FSafeSaveStyle::Get()
{
	return *StyleInstance;
}

FName FSafeSaveStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("SafeSaveStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FSafeSaveStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	// 1. Locate the Resources folder within the Plugin directory
	// Result: .../Plugins/SafeSave/Resources/
	FString ContentDir = IPluginManager::Get().FindPlugin("SafeSave")->GetBaseDir() / TEXT("Resources");
	Style->SetContentRoot(ContentDir);

	// 2. Helper Lambda to make loading brushes cleaner
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon128x128(128.0f, 128.0f);

	auto ImageBrush = [&](const FString& RelativePath, const FVector2D& ImageSize)
	{
		return new FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), ImageSize);
	};

	// 3. Register your Icons here
	// Assuming you have 'Icon128.png' in the Resources folder
	Style->Set("SafeSave.PluginIcon", ImageBrush(TEXT("Icon128"), Icon128x128));

	// Example: If you create custom traffic light icons later
	// Style->Set("SafeSave.Status.Green", ImageBrush(TEXT("Status_Green"), Icon16x16));
	// Style->Set("SafeSave.Status.Red",   ImageBrush(TEXT("Status_Red"),   Icon16x16));

	return Style;
}