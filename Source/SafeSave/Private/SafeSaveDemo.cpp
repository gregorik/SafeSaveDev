#include "SafeSaveDemo.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "FileHelpers.h"

void FSafeSaveDemo::RegisterCommands()
{
	// Registers console command: 'SafeSave.StressTest'
	if (GUnrealEd)
	{
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("SafeSave.StressTest"),
			TEXT("Generates 1000 dirty actors to test SafeSave performance."),
			FConsoleCommandDelegate::CreateStatic(&FSafeSaveDemo::GenerateStressScene),
			ECVF_Default
		);
	}
}

void FSafeSaveDemo::GenerateStressScene()
{
	// 1. Create/Reset Map
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	GEditor->BeginTransaction(FText::FromString("SafeSave Stress Test"));

	// 2. Procedural Generation parameters (Fibonacci Spiral)
	const int32 NumActors = 1000;
	const float GoldenRatio = (1.0f + FMath::Sqrt(5.0f)) / 2.0f;
	const float AngleIncrement = UE_TWO_PI * GoldenRatio;
	const float RadiusScale = 20.0f;

	// Load a basic cube
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	for (int32 i = 0; i < NumActors; i++)
	{
		float Dist = FMath::Sqrt((float)i) * RadiusScale;
		float Angle = i * AngleIncrement;

		FVector Location;
		Location.X = FMath::Cos(Angle) * Dist;
		Location.Y = FMath::Sin(Angle) * Dist;
		Location.Z = (float)i * 2.0f; // Rising spiral

		FRotator Rotation = FRotator(0, FMath::RadiansToDegrees(Angle), 0);

		FActorSpawnParameters SpawnParams;
		AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParams);

		if (NewActor && CubeMesh)
		{
			NewActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
			NewActor->SetActorLabel(FString::Printf(TEXT("Stress_Cube_%d"), i));

			// 3. FORCE DIRTY
			// This modifies the package, triggering SafeSave's unsaved asset detection.
			NewActor->Modify();
		}
	}

	GEditor->EndTransaction();

	UE_LOG(LogTemp, Warning, TEXT("[SafeSave] Stress Scene Generated. 1000 Actors Dirty. Check UI for Lag."));
}
