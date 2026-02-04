// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Manages the visual styling resources (Icons, Brushes) for the SafeSave plugin.
 * 
 * Usage:
 * const FSlateBrush* Icon = FSafeSaveStyle::Get().GetBrush("SafeSave.Icon");
 */
class FSafeSaveStyle
{
public:
	// Singleton Access
	static void Initialize();
	static void Shutdown();

	// Reloads textures (Useful if you edit the PNGs while Editor is running)
	static void ReloadTextures();

	// Accessor for the Style Set
	static const ISlateStyle& Get();

	// The Name of this Style Set (used for registration)
	static FName GetStyleSetName();

private:
	// Internal helper to construct the style set
	static TSharedRef<class FSlateStyleSet> Create();

	// The Singleton Instance
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};