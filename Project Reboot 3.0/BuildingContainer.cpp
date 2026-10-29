#include "BuildingContainer.h"
#include "FortPickup.h"
#include "FortLootPackage.h"
#include "FortGameModeAthena.h"
#include "gui.h"
#include "KismetStringLibrary.h"

bool ABuildingContainer::SpawnLoot(AFortPawn* Pawn)
{
	if (!Pawn)
		return false;

	this->ForceNetUpdate();

	auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
	// idk
   /* this->GetActorForwardVector() * this->GetLootSpawnLocation_Athena().X + this->GetActorRightVector() * this->GetLootSpawnLocation_Athena().Y + this->GetActorUpVector() * this->GetLootSpawnLocation_Athena().Z*/

	FVector LocationToSpawnLoot = this->GetActorLocation() + this->GetActorForwardVector() * this->GetLootSpawnLocation_Athena().X + this->GetActorRightVector() * this->GetLootSpawnLocation_Athena().Y + this->GetActorUpVector() * this->GetLootSpawnLocation_Athena().Z;

	auto RedirectedLootTier = GameMode->RedirectLootTier(GetSearchLootTierGroup());

	// LOG_INFO(LogInteraction, "RedirectedLootTier: {}", RedirectedLootTier.ToString());

	auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

	auto LootDrops = PickLootDrops(RedirectedLootTier, GameState->GetWorldLevel(), -1, bDebugPrintLooting);

	// HyperFN: the tier roll can land on an empty package, and some chest variants point at tier groups
	// that do not exist in this playlist; never let a chest open empty.
	for (int Attempt = 0; LootDrops.empty() && Attempt < 3; ++Attempt)
		LootDrops = PickLootDrops(RedirectedLootTier, GameState->GetWorldLevel(), -1, bDebugPrintLooting);

	bool bUsedFallback = false;

	if (LootDrops.empty() && this->GetName().contains("Chest"))
	{
		static auto TreasureTier = UKismetStringLibrary::Conv_StringToName(L"Loot_AthenaTreasure");
		LootDrops = PickLootDrops(TreasureTier, GameState->GetWorldLevel(), -1, bDebugPrintLooting);
		bUsedFallback = true;
	}

	// HyperFN: one line per opened container.
	LOG_INFO(LogInteraction, "[HyperFN] Container {} tier {} -> {} | world level {} | {} drops{}", this->GetName(), GetSearchLootTierGroup().ToString(), RedirectedLootTier.ToString(), GameState->GetWorldLevel(), LootDrops.size(), bUsedFallback ? " (fallback Loot_AthenaTreasure)" : "");

	for (auto& lootDrop : LootDrops)
	{
		PickupCreateData CreateData;
		CreateData.bToss = true;
		// CreateData.PawnOwner = Pawn;
		CreateData.ItemEntry = lootDrop.ItemEntry;
		CreateData.SpawnLocation = LocationToSpawnLoot;
		CreateData.SourceType = EFortPickupSourceTypeFlag::GetContainerValue();
		CreateData.bRandomRotation = true;
		CreateData.bShouldFreeItemEntryWhenDeconstructed = true;

		auto NewPickup = AFortPickup::SpawnPickup(CreateData);
	}

	if (!this->IsDestroyed())
	{
		this->ForceNetUpdate();
		// a buncha other stuff
	}

	return true;
}