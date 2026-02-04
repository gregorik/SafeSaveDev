#pragma once
#include "CoreMinimal.h"

class FSafeSaveDemo
{
public:
	static void RegisterCommands();

private:
	// Generates 1,000 actors in a spiral and dirties them to test UI performance
	static void GenerateStressScene();
};
