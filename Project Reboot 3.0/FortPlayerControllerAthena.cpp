#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawn.h"
#include "FortKismetLibrary.h"

#include "SoftObjectPtr.h"
#include "globals.h"
#include "GameplayStatics.h"
#include "hooking.h"
#include "FortAthenaMutator_GiveItemsAtGamePhaseStep.h"
#include "DataTableFunctionLibrary.h"
#include "AthenaResurrectionComponent.h"
#include "FortAthenaMutator_InventoryOverride.h"
#include "FortGadgetItemDefinition.h"
#include "gui.h"
#include "HyperFN.h"
#include "KismetStringLibrary.h"
#include <map>

void AFortPlayerControllerAthena::StartGhostModeHook(UObject* Context, FFrame* Stack, void* Ret)
{
	LOG_INFO(LogDev, __FUNCTION__);

	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	UFortWorldItemDefinition* ItemProvidingGhostMode = nullptr;

	Stack->StepCompiledIn(&ItemProvidingGhostMode);

	if (!ItemProvidingGhostMode)
	{
		LOG_INFO(LogDev, "Null item!");
		return StartGhostModeOriginal(Context, Stack, Ret);
	}

	if (!PlayerController->HasAuthority()) // for real
		return StartGhostModeOriginal(Context, Stack, Ret);
	
	LOG_INFO(LogDev, "Attempting to give item {}", ItemProvidingGhostMode->IsValidLowLevel() ? ItemProvidingGhostMode->GetFullName() : "BadRead");

	auto GhostModeRepData = PlayerController->GetGhostModeRepData();

	if (GhostModeRepData->IsInGhostMode())
	{
		LOG_INFO(LogDev, "Player is already in ghost mode!");
		return StartGhostModeOriginal(Context, Stack, Ret);
	}

	auto WorldInventory = PlayerController->GetWorldInventory();
	
	if (!WorldInventory)
		return StartGhostModeOriginal(Context, Stack, Ret);

	auto PickaxeInstance = WorldInventory->GetPickaxeInstance();

	// LOG_INFO(LogDev, "PickaxeInstance: {}", __int64(PickaxeInstance));

	if (PickaxeInstance)
	{
		WorldInventory->RemoveItem(PickaxeInstance->GetItemEntry()->GetItemGuid(), nullptr, REMOVE_ALL_ITEMS, true);
		// WorldInventory->Update();
	}

	bool bShouldUpdate = false;
	auto NewAndModifiedInstances = WorldInventory->AddItem(ItemProvidingGhostMode, &bShouldUpdate, 1);
	auto GhostModeItemInstance = NewAndModifiedInstances.first[0];

	if (!GhostModeItemInstance)
		return StartGhostModeOriginal(Context, Stack, Ret);

	// if (bShouldUpdate)
	WorldInventory->Update();

	PlayerController->AddPickaxeToInventory();
	WorldInventory->Update();

	PlayerController->ServerExecuteInventoryItemHook(PlayerController, GhostModeItemInstance->GetItemEntry()->GetItemGuid());
	PlayerController->ClientEquipItem(GhostModeItemInstance->GetItemEntry()->GetItemGuid(), true);
	LOG_INFO(LogDev, "Finished!");

	return StartGhostModeOriginal(Context, Stack, Ret);
}

void AFortPlayerControllerAthena::EndGhostModeHook(AFortPlayerControllerAthena* PlayerController)
{
	// I believe there are a lot of other places we should remove it (go to XREFs of K2_RemoveItemFromPlayer on a version like 6.21, and there will be something checking ghost stuff).

	LOG_INFO(LogDev, __FUNCTION__);

	if (!PlayerController->HasAuthority()) // for real
		return EndGhostModeOriginal(PlayerController);

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return EndGhostModeOriginal(PlayerController);

	FGhostModeRepData* GhostModeRepData = PlayerController->GetGhostModeRepData();
	UFortWorldItemDefinition* GhostModeItemDef = GhostModeRepData->GetGhostModeItemDef();

	LOG_INFO(LogDev, "GhostModeItemDef: {}", GhostModeItemDef->IsValidLowLevel() ? GhostModeItemDef->GetFullName() : "BadRead");

	if (!GhostModeItemDef) // bro IDFK
	{
		GhostModeItemDef = FindObject<UFortWorldItemDefinition>("/Game/Athena/Items/Gameplay/SpookyMist/AGID_SpookyMist.AGID_SpookyMist");
	}

	if (!GhostModeItemDef)
		return EndGhostModeOriginal(PlayerController);

	auto GhostModeItemInstance = WorldInventory->FindItemInstance(GhostModeItemDef);

	LOG_INFO(LogDev, "GhostModeItemInstance: {}", GhostModeItemInstance->IsValidLowLevel() ? GhostModeItemInstance->GetFullName() : "BadRead");

	if (!GhostModeItemInstance)
		return EndGhostModeOriginal(PlayerController);

	auto PickaxeInstance = PlayerController->AddPickaxeToInventory();

	WorldInventory->ForceNetUpdate();
	PlayerController->ForceNetUpdate();

	bool bShouldUpdate = false;
	int Count = GhostModeItemInstance->GetItemEntry()->GetCount(); // 1
	bool bForceRemoval = true; // false
	WorldInventory->RemoveItem(GhostModeItemInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, Count, bForceRemoval);

	// if (bShouldUpdate)
	WorldInventory->Update();

	if (PickaxeInstance)
	{
		PlayerController->ClientEquipItem(PickaxeInstance->GetItemEntry()->GetItemGuid(), true);
	}

	return EndGhostModeOriginal(PlayerController);
}

void AFortPlayerControllerAthena::ServerCreativeSetFlightSpeedIndexHook(UObject* Context, FFrame* Stack)
{
	int Index;
	Stack->StepCompiledIn(&Index);

	// LOG_INFO(LogDev, "Player {} wanting to change creative flight speed at index {}", Context->GetName(), Index);

	static auto WantedFlightSpeedChangedFn = FindObject<UFunction>("/Script/FortniteGame.FortPlayerControllerGameplay:OnRep_FlyingModifierIndex");

	if (!WantedFlightSpeedChangedFn)
	{
		return;
	}

	static auto FlyingModifierIndexOffset = Context->GetOffset("FlyingModifierIndex");
	Context->Get<int>(FlyingModifierIndexOffset) = Index;

	return Context->ProcessEvent(WantedFlightSpeedChangedFn);
}


void AFortPlayerControllerAthena::EnterAircraftHook(UObject* PC, AActor* Aircraft)
{
	auto PlayerController = Cast<AFortPlayerController>(Engine_Version < 424 ? PC : ((UActorComponent*)PC)->GetOwner());
	
	if (!PlayerController)
		return;

	// LOG_INFO(LogDev, "EnterAircraftHook");

	EnterAircraftOriginal(PC, Aircraft);

	// TODO Check if the player successfully got in the aircraft.

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	std::vector<std::pair<FGuid, int>> GuidAndCountsToRemove;

	auto& InventoryList = WorldInventory->GetItemList();

	auto& ItemInstances = InventoryList.GetItemInstances();

	for (int i = 0; i < ItemInstances.Num(); ++i)
	{
		auto ItemEntry = ItemInstances.at(i)->GetItemEntry();
		auto ItemDefinition = Cast<UFortWorldItemDefinition>(ItemEntry->GetItemDefinition());

		if (!ItemDefinition)
			continue;

		if (!ItemDefinition->CanBeDropped())
			continue;

		GuidAndCountsToRemove.push_back({ ItemEntry->GetItemGuid(), ItemEntry->GetCount() });
	}

	for (auto& Pair : GuidAndCountsToRemove)
	{
		WorldInventory->RemoveItem(Pair.first, nullptr, Pair.second, true);
	}

	std::vector<std::pair<AFortAthenaMutator*, UFunction*>> FunctionsToCall;
	LoopMutators([&](AFortAthenaMutator* Mutator) { FunctionsToCall.push_back(std::make_pair(Mutator, Mutator->FindFunction("OnGamePhaseStepChanged"))); });

	auto HandleGiveItemsAtGamePhaseStepMutator = [&](AFortAthenaMutator* Mutator) {
		if (auto GiveItemsAtGamePhaseStepMutator = Cast<AFortAthenaMutator_GiveItemsAtGamePhaseStep>(Mutator))
		{
			auto PhaseToGive = GiveItemsAtGamePhaseStepMutator->GetPhaseToGiveItems();
			auto& ItemsToGive = GiveItemsAtGamePhaseStepMutator->GetItemsToGive();

			LOG_INFO(LogDev, "PhaseToGiveItems: {} ItemsToGive.Num(): {}", (int)PhaseToGive, ItemsToGive.Num());

			if (PhaseToGive <= 5) // Flying or lower
			{
				for (int j = 0; j < ItemsToGive.Num(); j++)
				{
					auto ItemToGive = ItemsToGive.AtPtr(j, FItemsToGive::GetStructSize());

					if (!ItemToGive->GetItemToDrop())
						continue;

					float Out2 = 0;

					if (!IsBadReadPtr(ItemToGive->GetNumberToGive().GetCurve().CurveTable, 8) && ItemToGive->GetNumberToGive().GetCurve().RowName.IsValid())
					{
						Out2 = UDataTableFunctionLibrary::EvaluateCurveTableRow(ItemToGive->GetNumberToGive().GetCurve().CurveTable, ItemToGive->GetNumberToGive().GetCurve().RowName, 0.f);
					}

					LOG_INFO(LogDev, "[{}] Out2: {} ItemToGive.ItemToDrop: {}", j, Out2, ItemToGive->GetItemToDrop()->IsValidLowLevel() ? ItemToGive->GetItemToDrop()->GetFullName() : "BadRead");

					if (!Out2) // ?
						continue;

					WorldInventory->AddItem(ItemToGive->GetItemToDrop(), nullptr, Out2);
				}
			}
		}
	};

	LoopMutators(HandleGiveItemsAtGamePhaseStepMutator);

	/* if (auto GGMutator = Cast<AFortAthenaMutator_GG>(Mutator))
	{
		auto& WeaponEntries = GGMutator->GetWeaponEntries();

		LOG_INFO(LogDev, "[{}] WeaponEntries.Num(): {}", i, WeaponEntries.Num());

		for (int j = 0; j < WeaponEntries.Num(); j++)
		{
			WorldInventory->AddItem(WeaponEntries.at(j).Weapon, nullptr, 1);
		}
	} */

	auto PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState());

	auto AddInventoryOverrideTeamLoadouts = [&](AFortAthenaMutator* Mutator)
	{
		if (auto InventoryOverride = Cast<AFortAthenaMutator_InventoryOverride>(Mutator))
		{
			auto TeamIndex = PlayerStateAthena->GetTeamIndex();
			auto LoadoutTeam = InventoryOverride->GetLoadoutTeamForTeamIndex(TeamIndex);

			if (LoadoutTeam.UpdateOverrideType == EAthenaInventorySpawnOverride::AircraftPhaseOnly)
			{
				auto LoadoutContainer = InventoryOverride->GetLoadoutContainerForTeamIndex(TeamIndex);

				for (int i = 0; i < LoadoutContainer.Loadout.Num(); ++i)
				{
					auto& ItemAndCount = LoadoutContainer.Loadout.at(i);
					WorldInventory->AddItem(ItemAndCount.GetItem(), nullptr, ItemAndCount.GetCount());
				}
			}
		}
	};

	LoopMutators(AddInventoryOverrideTeamLoadouts);

	WorldInventory->Update();
	
	// Should we equip the pickaxe for older builds here?

	if (Fortnite_Version < 2.5) // idk
	{
		/* auto PickaxeInstance = WorldInventory->GetPickaxeInstance();

		if (!PickaxeInstance)
			return;
				
		AFortPlayerController::ServerExecuteInventoryItemHook(PlayerController, PickaxeInstance->GetItemEntry()->GetItemGuid()); */
	}
}

void AFortPlayerControllerAthena::ServerRequestSeatChangeHook(AFortPlayerControllerAthena* PlayerController, int TargetSeatIndex)
{
	auto Pawn = Cast<AFortPlayerPawn>(PlayerController->GetPawn());
	
	if (!Pawn)
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

	auto Vehicle = Pawn->GetVehicle();

	if (!Vehicle)
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

	auto OldVehicleWeaponDefinition = Pawn->GetVehicleWeaponDefinition(Vehicle);
	
	LOG_INFO(LogDev, "OldVehicleWeaponDefinition: {}", OldVehicleWeaponDefinition ? OldVehicleWeaponDefinition->GetFullName() : "BadRead");

	if (!OldVehicleWeaponDefinition)
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

	auto OldVehicleWeaponInstance = WorldInventory->FindItemInstance(OldVehicleWeaponDefinition);

	if (OldVehicleWeaponInstance)
	{
		bool bShouldUpdate = false;
		WorldInventory->RemoveItem(OldVehicleWeaponInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, OldVehicleWeaponInstance->GetItemEntry()->GetCount(), true);

		if (bShouldUpdate)
			WorldInventory->Update();
	}

	auto RequestingVehicleWeaponDefinition = Vehicle->GetVehicleWeaponForSeat(TargetSeatIndex);

	if (!RequestingVehicleWeaponDefinition)
	{
		auto PickaxeInstance = WorldInventory->GetPickaxeInstance();

		if (!PickaxeInstance)
			return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

		AFortPlayerController::ServerExecuteInventoryItemHook(PlayerController, PickaxeInstance->GetItemEntry()->GetItemGuid()); // Bad, we should equip the last weapon.
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);
	}

	auto NewAndModifiedInstances = WorldInventory->AddItem(RequestingVehicleWeaponDefinition, nullptr);
	auto RequestedVehicleInstance = NewAndModifiedInstances.first[0];

	if (!RequestedVehicleInstance)
		return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);

	WorldInventory->Update();

	auto RequestedVehicleWeapon = Pawn->EquipWeaponDefinition(RequestingVehicleWeaponDefinition, RequestedVehicleInstance->GetItemEntry()->GetItemGuid());

	return ServerRequestSeatChangeOriginal(PlayerController, TargetSeatIndex);
}

void AFortPlayerControllerAthena::ServerRestartPlayerHook(AFortPlayerControllerAthena* Controller)
{
	static auto FortPlayerControllerZoneDefault = FindObject<UClass>(L"/Script/FortniteGame.Default__FortPlayerControllerZone");
	static auto ServerRestartPlayerFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ServerRestartPlayer");
	static auto ZoneServerRestartPlayer = __int64(FortPlayerControllerZoneDefault->VFTable[GetFunctionIdxOrPtr(ServerRestartPlayerFn) / 8]);
	static void (*ZoneServerRestartPlayerOriginal)(AFortPlayerController*) = decltype(ZoneServerRestartPlayerOriginal)(__int64(ZoneServerRestartPlayer));
	
	// auto NAME_Spectating = UKismetStringLibrary::Conv_StringToName(L"NAME_Spectating");

	// LOG_INFO(LogDev, "ISplayerwaiting: {}", Controller->IsPlayerWaiting());

	// Controller->GetStateName() = NAME_Spectating;
	// Controller->SetPlayerIsWaiting(true);

	LOG_INFO(LogDev, "ServerRestartPlayerHook Call 0x{:x} returning with 0x{:x}!", ZoneServerRestartPlayer - __int64(_ReturnAddress()), __int64(ZoneServerRestartPlayerOriginal) - __int64(GetModuleHandleW(0)));
	return ZoneServerRestartPlayerOriginal(Controller);
}

void AFortPlayerControllerAthena::ServerGiveCreativeItemHook(AFortPlayerControllerAthena* Controller, FFortItemEntry CreativeItem)
{
	// Don't worry, the validate has a check if it is a creative enabled mode or not, but we need to add a volume check and permission check I think.

	auto CreativeItemPtr = &CreativeItem;
	auto ItemDefinition = CreativeItemPtr->GetItemDefinition();

	if (!ItemDefinition)
		return;

	bool bShouldUpdate = false;
	auto LoadedAmmo = -1; // CreativeItemPtr->GetLoadedAmmo()
	Controller->GetWorldInventory()->AddItem(ItemDefinition, &bShouldUpdate, CreativeItemPtr->GetCount(), LoadedAmmo, false);

	if (bShouldUpdate)
		Controller->GetWorldInventory()->Update(Controller);
}

void AFortPlayerControllerAthena::ServerTeleportToPlaygroundLobbyIslandHook(AFortPlayerControllerAthena* Controller)
{
	LOG_INFO(LogDev, "ServerTeleportToPlaygroundLobbyIslandHook!");

	auto Pawn = Controller->GetMyFortPawn();

	if (!Pawn)
		return;

	// TODO IsTeleportToCreativeHubAllowed

	static auto FortPlayerStartCreativeClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");
	auto AllCreativePlayerStarts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), FortPlayerStartCreativeClass);

	for (int i = 0; i < AllCreativePlayerStarts.Num(); ++i)
	{
		auto CurrentPlayerStart = AllCreativePlayerStarts.at(i);

		static auto PlayerStartTagsOffset = CurrentPlayerStart->GetOffset("PlayerStartTags");
		auto bHasSpawnTag = CurrentPlayerStart->Get<FGameplayTagContainer>(PlayerStartTagsOffset).Contains("Playground.LobbyIsland.Spawn");

		if (!bHasSpawnTag)
			continue;

		Pawn->TeleportTo(CurrentPlayerStart->GetActorLocation(), Pawn->GetActorRotation());
		break;
	}

	AllCreativePlayerStarts.Free();
}

// HyperFN: the arena Hype counter. Every player has a FortBroadcastRemoteClientInfo whose RemoteEventScore is the event
// score the game shows for them (OnRep_RemoteEventScore -> OnPlayerEventScoreChanged). On arena servers the backend
// writes the player's Hype and their division's points into their locker file (hype, arenabus, arenaelim); the counter
// starts at their Hype and follows the match: bus fare when the bus leaves, eliminations, placement tiers.
int HyperFNLoadoutValue(AFortPlayerController* Controller, const char* Key, int Default)
{
	auto PlayerState = Controller ? Cast<AFortPlayerState>(Controller->GetPlayerState()) : nullptr;

	if (!PlayerState)
		return Default;

	auto Loadout = HyperFN::ReadKeyValues("hyperfn\\loadouts\\" + PlayerState->GetPlayerName().ToString() + ".txt");
	return Loadout.count(Key) && !Loadout[Key].empty() ? std::atoi(Loadout[Key].c_str()) : Default;
}

static AActor* HyperFNEventScoreInfo(AFortPlayerController* Controller, int*& OutScore)
{
	OutScore = nullptr;
	static auto InfoOffset = Controller->GetOffset("BroadcastRemoteClientInfo", false);
	auto Info = InfoOffset != -1 ? Controller->Get<AActor*>(InfoOffset) : nullptr;

	if (!Info)
		return nullptr;

	static auto ScoreOffset = Info->GetOffset("RemoteEventScore", false);

	if (ScoreOffset == -1)
		return nullptr;

	OutScore = (int*)(__int64(Info) + ScoreOffset);
	return Info;
}

void HyperFNAddEventScore(AFortPlayerController* Controller, int Delta, const char* Why)
{
	int* Score = nullptr;
	auto Info = Controller ? HyperFNEventScoreInfo(Controller, Score) : nullptr;

	if (!Info || !Score || !Delta || HyperFNLoadoutValue(Controller, "hype", -1) < 0)
		return;

	*Score += Delta;
	Info->ForceNetUpdate();
	LOG_INFO(LogDev, "[HyperFN] Arena counter: {} {} -> {}", Why, Delta, *Score);
}

// Once per player: the counter starts at their Hype.
static void HyperFNStartEventScore(AFortPlayerController* Controller)
{
	static std::set<AFortPlayerController*> Started;
	int Hype = HyperFNLoadoutValue(Controller, "hype", -1);
	int* Score = nullptr;
	auto Info = Hype >= 0 && !Started.count(Controller) ? HyperFNEventScoreInfo(Controller, Score) : nullptr;

	if (!Info || !Score)
		return;

	Started.insert(Controller);
	LOG_INFO(LogDev, "[HyperFN] Arena counter: starts at {} Hype (was {})", Hype, *Score);
	*Score = Hype;
	Info->ForceNetUpdate();
}

// HyperFN: the arena Hype tile on the HUD. The HUD adds its arena widgets (ArenaTournamentUIExtensions) only when the
// client's tournament manager knows the event the match belongs to, and the client learns it from
// ClientNotifyMatchEntered (kept only with bUsePlayingEventIds=true in DefaultRuntimeOptions.ini). The backend writes
// the arena player's event and event window into their locker file (eventid, windowid); everyone else gets empty ids,
// which clears what an earlier arena match left on their client.
// The HUD looks for the event once, when it is built, which is about when the player's pawn spawns; so this goes out
// from HandleStartingNewPlayer (seconds earlier) and once more before the pawn spawns, in case the first was too early.
void HyperFNNotifyMatchEntered(AFortPlayerController* Controller)
{
	static std::map<AFortPlayerController*, int> Sent;
	auto PlayerState = Controller ? Cast<AFortPlayerState>(Controller->GetPlayerState()) : nullptr;

	if (!PlayerState || Sent[Controller] >= 2)
		return;

	// A client RPC of FortPlayerStateAthena. Its one parameter is the event ids struct (64 bytes in 14.60): the event id,
	// then the event window id, then fields the client does not need here, left empty.
	static auto NotifyFn = PlayerState->FindFunction("ClientNotifyMatchEntered");
	int ParamsSize = NotifyFn ? NotifyFn->GetPropertiesSize() : 0;

	if (ParamsSize < (int)sizeof(FString) * 2 || ParamsSize > 0x100)
	{
		static bool bLogged = false;

		if (!bLogged)
			LOG_INFO(LogDev, "[HyperFN] Arena HUD: ClientNotifyMatchEntered {}", NotifyFn ? "has unexpected parameters (" + std::to_string(NotifyFn->GetPropertiesSize()) + " bytes)" : "not found");

		bLogged = true;
		return;
	}

	auto Loadout = HyperFN::ReadKeyValues("hyperfn\\loadouts\\" + PlayerState->GetPlayerName().ToString() + ".txt");
	std::wstring EventId(Loadout["eventid"].begin(), Loadout["eventid"].end());
	std::wstring WindowId(Loadout["windowid"].begin(), Loadout["windowid"].end());

	alignas(16) unsigned char Params[0x100] = {};
	new (Params) FString(EventId.c_str());
	new (Params + sizeof(FString)) FString(WindowId.c_str());
	PlayerState->ProcessEvent(NotifyFn, Params);
	Sent[Controller]++;

	LOG_INFO(LogDev, "[HyperFN] Arena HUD: {} entered event '{}' window '{}' ({} byte parameters)", PlayerState->GetPlayerName().ToString(), Loadout["eventid"], Loadout["windowid"], ParamsSize);
}

// HyperFN: the skin's own parts (head, body, ...) chosen one by one like the backpack. ApplyCID reports success
// even when the parts did not take (CID_333 Squishy stayed on the default body), so this also logs what loaded.
static void HyperFNChooseCharacterParts(AFortPlayerPawn* Pawn, UObject* CID, const std::string& PlayerName)
{
	static auto HeroDefinitionOffset = CID->GetOffset("HeroDefinition", false);
	auto HeroDefinition = HeroDefinitionOffset != -1 ? CID->Get(HeroDefinitionOffset) : nullptr;

	if (!HeroDefinition)
	{
		LOG_INFO(LogDev, "[HyperFN] {} skin parts: no hero definition", PlayerName);
		return;
	}

	static auto SpecializationsOffset = HeroDefinition->GetOffset("Specializations");
	auto& Specializations = HeroDefinition->Get<TArray<TSoftObjectPtr<UObject>>>(SpecializationsOffset);
	static auto SpecializationClass = FindObject<UClass>(L"/Script/FortniteGame.FortHeroSpecialization");
	static auto CustomCharacterPartClass = FindObject<UClass>(L"/Script/FortniteGame.CustomCharacterPart");
	std::string Parts;

	for (int i = 0; i < Specializations.Num(); i++)
	{
		auto Specialization = Specializations.at(i, GetSoftObjectSize()).Get(SpecializationClass, true);

		if (!Specialization)
		{
			Parts += " (specialization not loaded)";
			continue;
		}

		static auto CharacterPartsOffset = Specialization->GetOffset("CharacterParts");
		auto& CharacterParts = Specialization->Get<TArray<TSoftObjectPtr<UObject>>>(CharacterPartsOffset);

		for (int z = 0; z < CharacterParts.Num(); z++)
		{
			auto& PartSoft = CharacterParts.at(z, GetSoftObjectSize());
			auto Part = PartSoft.Get(CustomCharacterPartClass, true);

			if (!Part)
			{
				Parts += " " + PartSoft.SoftObjectPtr.ObjectID.AssetPathName.ToString() + " (not loaded)";
				continue;
			}

			static auto CharacterPartTypeOffset = Part->GetOffset("CharacterPartType");
			Pawn->ServerChoosePart((EFortCustomPartType)Part->Get<uint8>(CharacterPartTypeOffset), Part);
			Parts += " " + Part->GetName();
		}
	}

	LOG_INFO(LogDev, "[HyperFN] {} skin parts:{}", PlayerName, Parts);
}

// HyperFN: the skin's styles. The backend writes "charactervariants=Material.Mat3;Parts.Stage2" (channel.active, as
// the locker saves them) into the loadout file; the game's tags are Cosmetics.Variant.Channel.<channel> and
// Cosmetics.Variant.Property.<active>.
using HyperFNVariants = std::vector<std::pair<std::string, std::string>>;

static HyperFNVariants HyperFNParseVariants(const std::string& Text)
{
	HyperFNVariants Out;
	size_t Start = 0;

	while (Start < Text.size())
	{
		auto End = Text.find(';', Start);
		if (End == std::string::npos) End = Text.size();
		auto Pair = Text.substr(Start, End - Start);
		auto Dot = Pair.find('.');
		if (Dot != std::string::npos && Dot > 0 && Dot + 1 < Pair.size())
			Out.push_back({ Pair.substr(0, Dot), Pair.substr(Dot + 1) });
		Start = End + 1;
	}

	return Out;
}

static FName HyperFNTag(const std::string& Tag)
{
	std::wstring Wide(Tag.begin(), Tag.end());
	return UKismetStringLibrary::Conv_StringToName(Wide.c_str());
}

static bool HyperFNSameName(const void* A, const FName& B) { return memcmp(A, &B, sizeof(FName)) == 0; }

// The styles into a loadout's CharacterVariantChannels (TArray<FMcpVariantChannelInfo>): the clients apply them when
// the pawn's loadout replicates (OnRep_CosmeticLoadout), material styles included. Returns how many were set, or -1
// when this build has no such fields.
static int HyperFNSetVariantChannels(void* Loadout, UObject* CID, const HyperFNVariants& Variants)
{
	static auto InfoStruct = FindObject<UStruct>(L"/Script/FortniteGame.McpVariantChannelInfo");
	static auto LoadoutStruct = FindObject<UStruct>(L"/Script/FortniteGame.FortAthenaLoadout");

	if (!Loadout || !InfoStruct || !LoadoutStruct)
		return -1;

	static auto ChannelsOffset = FindOffsetStruct("/Script/FortniteGame.FortAthenaLoadout", "CharacterVariantChannels", false);
	static auto ChannelTagOffset = FindOffsetStruct("/Script/FortniteGame.McpVariantChannelInfo", "VariantChannelTag", false);
	static auto ActiveTagOffset = FindOffsetStruct("/Script/FortniteGame.McpVariantChannelInfo", "ActiveVariantTag", false);
	static auto UsedForOffset = FindOffsetStruct("/Script/FortniteGame.McpVariantChannelInfo", "ItemVariantIsUsedFor", false);
	static auto InfoSize = InfoStruct->GetPropertiesSize();

	if (ChannelsOffset <= 0 || ChannelTagOffset < 0 || ActiveTagOffset < 0 || InfoSize <= 0)
		return -1;

	auto& Channels = *(TArray<uint8_t>*)(__int64(Loadout) + ChannelsOffset);
	Channels.ArrayNum = 0;
	std::vector<uint8_t> Info(InfoSize);

	for (auto& [Channel, Active] : Variants)
	{
		std::fill(Info.begin(), Info.end(), 0);
		*(FName*)(Info.data() + ChannelTagOffset) = HyperFNTag("Cosmetics.Variant.Channel." + Channel);
		*(FName*)(Info.data() + ActiveTagOffset) = HyperFNTag("Cosmetics.Variant.Property." + Active);

		if (UsedForOffset > 0)
			*(UObject**)(Info.data() + UsedForOffset) = CID;

		Channels.AddPtr(Info.data(), InfoSize);
	}

	return Channels.Num();
}

// Styles that swap parts (Parts.Stage2 and the like): the skin's FortCosmeticCharacterPartVariant lists the parts of
// each option, and the server chooses them like the base parts so everyone sees them.
static std::string HyperFNChooseVariantParts(AFortPlayerPawn* Pawn, UObject* CID, const HyperFNVariants& Variants)
{
	static auto ItemVariantsOffset = CID->GetOffset("ItemVariants", false);
	static auto PartDefStruct = FindObject<UStruct>(L"/Script/FortniteGame.PartVariantDef");

	if (ItemVariantsOffset == -1 || !PartDefStruct)
		return " (this build has no part styles)";

	static auto PartDefSize = PartDefStruct->GetPropertiesSize();
	static auto CustomizationTagOffset = FindOffsetStruct("/Script/FortniteGame.PartVariantDef", "CustomizationVariantTag", false);
	static auto VariantPartsOffset = FindOffsetStruct("/Script/FortniteGame.PartVariantDef", "VariantParts", false);
	static auto CustomCharacterPartClass = FindObject<UClass>(L"/Script/FortniteGame.CustomCharacterPart");

	if (CustomizationTagOffset < 0 || VariantPartsOffset <= 0)
		return " (part style fields not found)";

	auto& ItemVariants = CID->Get<TArray<UObject*>>(ItemVariantsOffset);
	std::string Chosen;

	for (int i = 0; i < ItemVariants.Num(); i++)
	{
		auto Variant = ItemVariants.at(i);

		if (!Variant)
			continue;

		auto ChannelOffset = Variant->GetOffset("VariantChannelTag", false);
		auto PartOptionsOffset = Variant->GetOffset("PartOptions", false);

		if (ChannelOffset == -1 || PartOptionsOffset == -1)
			continue;

		for (auto& [Channel, Active] : Variants)
		{
			if (!HyperFNSameName(Variant->GetPtr<FName>(ChannelOffset), HyperFNTag("Cosmetics.Variant.Channel." + Channel)))
				continue;

			auto ActiveTag = HyperFNTag("Cosmetics.Variant.Property." + Active);
			auto& Options = Variant->Get<TArray<uint8_t>>(PartOptionsOffset);

			for (int o = 0; o < Options.Num(); o++)
			{
				auto Option = (uint8_t*)Options.AtPtr(o, PartDefSize);

				if (!HyperFNSameName(Option + CustomizationTagOffset, ActiveTag))
					continue;

				auto& Parts = *(TArray<TSoftObjectPtr<UObject>>*)(Option + VariantPartsOffset);

				for (int p = 0; p < Parts.Num(); p++)
				{
					auto Part = Parts.at(p, GetSoftObjectSize()).Get(CustomCharacterPartClass, true);

					if (!Part)
						continue;

					static auto CharacterPartTypeOffset = Part->GetOffset("CharacterPartType");
					Pawn->ServerChoosePart((EFortCustomPartType)Part->Get<uint8>(CharacterPartTypeOffset), Part);
					Chosen += " " + Part->GetName();
				}
			}
		}
	}

	return Chosen.empty() ? " (none)" : Chosen;
}

// HyperFN: without MCP the server never reads the player's locker, so everyone spawned in the default skin.
// The backend writes hyperfn\loadouts\<PlayerName>.txt when it sends a player here; apply it like "cheat applycid".
// Called on possession and again when the loading screen drops: ApplyCosmeticLoadout runs there and puts the
// default skin back on in the warmup island.
void HyperFNApplyLoadout(AFortPlayerController* ControllerAsFort, AFortPlayerPawn* PawnAsFort, AFortPlayerState* PlayerStateAsFort)
{
		if (ControllerAsFort && PawnAsFort && PlayerStateAsFort)
		{
			auto PlayerName = PlayerStateAsFort->GetPlayerName().ToString();
			auto Loadout = HyperFN::ReadKeyValues("hyperfn\\loadouts\\" + PlayerName + ".txt");
			auto Variants = HyperFNParseVariants(Loadout["charactervariants"]);

			if (Loadout.count("character") && !Loadout["character"].empty())
			{
				auto CID = FindObject(Loadout["character"], nullptr, ANY_PACKAGE);

				if (!CID)
				{
					static auto CharacterDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaCharacterItemDefinition");
					CID = LoadObject("/Game/Athena/Items/Cosmetics/Characters/" + Loadout["character"] + "." + Loadout["character"], CharacterDefinitionClass);
				}

				LOG_INFO(LogDev, "[HyperFN] {} skin {} -> {}", PlayerName, Loadout["character"], CID ? (ApplyCID(PawnAsFort, CID) ? "applied" : "apply failed") : "not found");

				if (CID)
					HyperFNChooseCharacterParts(PawnAsFort, CID, PlayerName);

				if (CID && !Variants.empty())
					LOG_INFO(LogDev, "[HyperFN] {} style {} parts:{}", PlayerName, Loadout["charactervariants"], HyperFNChooseVariantParts(PawnAsFort, CID, Variants));
			}

			if (Loadout.count("backpack") && !Loadout["backpack"].empty())
			{
				auto BID = FindObject(Loadout["backpack"], nullptr, ANY_PACKAGE);

				if (!BID)
				{
					static auto BackpackDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaBackpackItemDefinition");
					BID = LoadObject("/Game/Athena/Items/Cosmetics/Backpacks/" + Loadout["backpack"] + "." + Loadout["backpack"], BackpackDefinitionClass);
				}

				auto CharacterPartsOffset = BID ? BID->GetOffset("CharacterParts") : -1;

				if (BID && CharacterPartsOffset != -1)
				{
					auto& BackpackCharacterParts = BID->Get<TArray<UObject*>>(CharacterPartsOffset);

					for (int i = 0; i < BackpackCharacterParts.Num(); ++i)
					{
						if (auto Part = BackpackCharacterParts.at(i))
							PawnAsFort->ServerChoosePart(EFortCustomPartType::Backpack, Part);
					}
				}

				LOG_INFO(LogDev, "[HyperFN] {} backpack {} -> {}", PlayerName, Loadout["backpack"], BID ? "applied" : "not found");
			}

			// Pickaxe, glider and contrail live in the controller's CosmeticLoadoutPC (AddPickaxeToInventory reads it),
			// and the pawn keeps its own copy that the clients use for the glider visuals.
			auto ControllerLoadout = ControllerAsFort ? ControllerAsFort->GetCosmeticLoadout() : nullptr;
			static auto Pawn_CosmeticLoadoutOffset = PawnAsFort->GetOffset("CosmeticLoadout", false);
			auto PawnLoadout = Pawn_CosmeticLoadoutOffset != -1 ? PawnAsFort->GetPtr<FFortAthenaLoadout>(Pawn_CosmeticLoadoutOffset) : nullptr;

			auto LoadCosmetic = [&](const std::string& Key, const std::string& Folder, const wchar_t* ClassName) -> UObject*
			{
				if (!Loadout.count(Key) || Loadout[Key].empty())
					return nullptr;

				auto& Id = Loadout[Key];
				auto Object = FindObject(Id + "." + Id, nullptr, ANY_PACKAGE);

				if (!Object)
					Object = LoadObject("/Game/Athena/Items/Cosmetics/" + Folder + "/" + Id + "." + Id, FindObject<UClass>(ClassName));

				LOG_INFO(LogDev, "[HyperFN] {} {} {} -> {}", PlayerName, Key, Id, Object ? "found" : "not found");
				return Object;
			};

			auto SetLoadoutField = [&](const std::string& Field, UObject* Value)
			{
				auto Offset = FindOffsetStruct("/Script/FortniteGame.FortAthenaLoadout", Field, false);

				if (Offset == -1 || !Value)
					return;

				if (ControllerLoadout)
					*(UObject**)(__int64(ControllerLoadout) + Offset) = Value;

				if (PawnLoadout)
					*(UObject**)(__int64(PawnLoadout) + Offset) = Value;
			};

			if (auto PID = LoadCosmetic("pickaxe", "Pickaxes", L"/Script/FortniteGame.AthenaPickaxeItemDefinition"))
			{
				static auto WeaponDefinitionOffset = FindOffsetStruct("/Script/FortniteGame.AthenaPickaxeItemDefinition", "WeaponDefinition");
				auto WeaponDefinition = PID->Get<UFortItemDefinition*>(WeaponDefinitionOffset);

				if (WeaponDefinition)
				{
					SetLoadoutField("Pickaxe", PID);

					// The default pickaxe may already be in the inventory; swap it like "cheat setpickaxe".
					auto WorldInventory = ControllerAsFort ? ControllerAsFort->GetWorldInventory() : nullptr;
					auto PickaxeInstance = WorldInventory ? WorldInventory->GetPickaxeInstance() : nullptr;

					if (PickaxeInstance && PickaxeInstance->GetItemEntry()->GetItemDefinition() != WeaponDefinition)
					{
						WorldInventory->RemoveItem(PickaxeInstance->GetItemEntry()->GetItemGuid(), nullptr, PickaxeInstance->GetItemEntry()->GetCount(), true);
						auto NewAndModifiedInstances = WorldInventory->AddItem(WeaponDefinition, nullptr, 1);
						WorldInventory->Update();

						if (NewAndModifiedInstances.first.size() > 0 && NewAndModifiedInstances.first[0])
							ControllerAsFort->ClientEquipItem(NewAndModifiedInstances.first[0]->GetItemEntry()->GetItemGuid(), true);
					}
				}
			}

			SetLoadoutField("Glider", LoadCosmetic("glider", "Gliders", L"/Script/FortniteGame.AthenaGliderItemDefinition"));
			SetLoadoutField("SkyDiveContrail", LoadCosmetic("contrail", "Contrails", L"/Script/FortniteGame.AthenaSkyDiveContrailItemDefinition"));

			// Keep the loadout struct in step with what we applied, so anything reading it gets our skin too.
			auto CharacterObject = LoadCosmetic("character", "Characters", L"/Script/FortniteGame.AthenaCharacterItemDefinition");
			SetLoadoutField("Character", CharacterObject);

			if (CharacterObject && !Variants.empty())
			{
				auto OnController = HyperFNSetVariantChannels(ControllerLoadout, CharacterObject, Variants);
				auto OnPawn = HyperFNSetVariantChannels(PawnLoadout, CharacterObject, Variants);
				LOG_INFO(LogDev, "[HyperFN] {} style {} in the loadout: controller {}, pawn {}", PlayerName, Loadout["charactervariants"], OnController, OnPawn);
			}
			SetLoadoutField("Backpack", LoadCosmetic("backpack", "Backpacks", L"/Script/FortniteGame.AthenaBackpackItemDefinition"));

			PlayerStateAsFort->ForceNetUpdate();
			PawnAsFort->ForceNetUpdate();
			ControllerAsFort->ForceNetUpdate();

			HyperFNStartEventScore(ControllerAsFort);
		}
}

// HyperFN: the player's locker into the controller only (CosmeticLoadoutPC), before their pawn exists. The pawn takes
// its glider and contrail from the controller when it is created; the lobby pawn was created before HyperFNApplyLoadout
// ran, so the lobby glider was the default one (the match pawn, made at the jump, already had the right one).
void HyperFNApplyControllerLoadout(AFortPlayerController* Controller, AFortPlayerState* PlayerState)
{
	auto ControllerLoadout = Controller && PlayerState ? Controller->GetCosmeticLoadout() : nullptr;

	if (!ControllerLoadout)
		return;

	auto PlayerName = PlayerState->GetPlayerName().ToString();
	auto Loadout = HyperFN::ReadKeyValues("hyperfn\\loadouts\\" + PlayerName + ".txt");
	int Set = 0;

	auto Apply = [&](const std::string& Key, const std::string& Folder, const wchar_t* ClassName, const std::string& Field)
	{
		if (!Loadout.count(Key) || Loadout[Key].empty())
			return;

		auto& Id = Loadout[Key];
		auto Object = FindObject(Id + "." + Id, nullptr, ANY_PACKAGE);

		if (!Object)
			Object = LoadObject("/Game/Athena/Items/Cosmetics/" + Folder + "/" + Id + "." + Id, FindObject<UClass>(ClassName));

		auto Offset = FindOffsetStruct("/Script/FortniteGame.FortAthenaLoadout", Field, false);

		if (Object && Offset != -1)
		{
			*(UObject**)(__int64(ControllerLoadout) + Offset) = Object;
			Set++;
		}
	};

	Apply("character", "Characters", L"/Script/FortniteGame.AthenaCharacterItemDefinition", "Character");
	Apply("backpack", "Backpacks", L"/Script/FortniteGame.AthenaBackpackItemDefinition", "Backpack");
	Apply("pickaxe", "Pickaxes", L"/Script/FortniteGame.AthenaPickaxeItemDefinition", "Pickaxe");
	Apply("glider", "Gliders", L"/Script/FortniteGame.AthenaGliderItemDefinition", "Glider");
	Apply("contrail", "Contrails", L"/Script/FortniteGame.AthenaSkyDiveContrailItemDefinition", "SkyDiveContrail");

	// The skin's styles too, so the pawn made from this loadout has them from the start.
	auto Variants = HyperFNParseVariants(Loadout["charactervariants"]);

	if (!Variants.empty())
	{
		static auto CharacterOffset = FindOffsetStruct("/Script/FortniteGame.FortAthenaLoadout", "Character", false);
		auto CID = CharacterOffset > 0 ? *(UObject**)(__int64(ControllerLoadout) + CharacterOffset) : nullptr;

		if (CID)
			HyperFNSetVariantChannels(ControllerLoadout, CID, Variants);
	}

	if (Set)
		LOG_INFO(LogDev, "[HyperFN] {} locker set on the controller before the pawn ({} items)", PlayerName, Set);

	// Before the client builds its HUD, so the arena widgets are added with it.
	HyperFNNotifyMatchEntered(Controller);
}

void AFortPlayerControllerAthena::ServerAcknowledgePossessionHook(APlayerController* Controller, APawn* Pawn)
{
	LOG_INFO(LogDev, "ServerAcknowledgePossession!");
	static auto AcknowledgedPawnOffset = Controller->GetOffset("AcknowledgedPawn");

	const APawn* OldAcknowledgedPawn = Controller->Get<APawn*>(AcknowledgedPawnOffset);
	Controller->Get<APawn*>(AcknowledgedPawnOffset) = Pawn;

	auto ControllerAsFort = Cast<AFortPlayerController>(Controller);
	auto PawnAsFort = Cast<AFortPlayerPawn>(Pawn);
	auto PlayerStateAsFort = Cast<AFortPlayerState>(Pawn->GetPlayerState());

	if (!PawnAsFort)
		return;

	if (OldAcknowledgedPawn != PawnAsFort)
	{
		PawnAsFort->SetShield(StartingShield);
	}

	if (Globals::bNoMCP)
	{
		HyperFNApplyLoadout(ControllerAsFort, PawnAsFort, PlayerStateAsFort);
		return;
	}

	ControllerAsFort->ApplyCosmeticLoadout();
}

void AFortPlayerControllerAthena::ServerPlaySquadQuickChatMessageHook(AFortPlayerControllerAthena* PlayerController, __int64 ChatEntry, __int64 SenderID)
{
	using UAthenaEmojiItemDefinition = UFortItemDefinition;

	auto PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState());

	if (!PlayerStateAthena)
		return;

	static auto IndexOffset = FindOffsetStruct("/Script/FortniteGame.AthenaQuickChatActiveEntry", "Index");
	auto Index = *(int8*)(__int64(ChatEntry) + IndexOffset);

	LOG_INFO(LogDev, "Index: {}", (int)Index);

	uint8 NewTeamMemberState = 0;

	switch (Index)
	{
	case 0:
		NewTeamMemberState = 8;
		break;
	case 1:
		NewTeamMemberState = 9;
		break;
	case 2:
		NewTeamMemberState = 11;
		break;
	case 3:
		NewTeamMemberState = 10;
		break;
	case 4:
		NewTeamMemberState = 12;
		break;
	case 5:
		NewTeamMemberState = 3;
		break;
	case 6:
		NewTeamMemberState = 4;
		break;
	case 7:
		NewTeamMemberState = 2;
		break;
	case 8:
		NewTeamMemberState = 5;
		break;
	case 9:
		NewTeamMemberState = 6;
		break;
	default:
		break;
	}

	NewTeamMemberState -= AmountToSubtractIndex;

	PlayerStateAthena->Get<uint8_t>("ReplicatedTeamMemberState") = NewTeamMemberState;
	PlayerStateAthena->Get<uint8_t>("TeamMemberState") = NewTeamMemberState; // pretty sure unneeded

	static auto EmojiComm = FindObject<UAthenaEmojiItemDefinition>(L"/Game/Athena/Items/Cosmetics/Dances/Emoji/Emoji_Comm.Emoji_Comm");
	PlayerController->ServerPlayEmoteItemHook(PlayerController, EmojiComm);

	static auto OnRep_ReplicatedTeamMemberStateFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_ReplicatedTeamMemberState");
	PlayerStateAthena->ProcessEvent(OnRep_ReplicatedTeamMemberStateFn);
}

void AFortPlayerControllerAthena::GetPlayerViewPointHook(AFortPlayerControllerAthena* PlayerController, FVector& Location, FRotator& Rotation)
{
	// I don't know why but GetActorEyesViewPoint only works on some versions.
	/* static auto GetActorEyesViewPointFn = FindObject<UFunction>(L"/Script/Engine.Actor.GetActorEyesViewPoint");
	static auto GetActorEyesViewPointIndex = GetFunctionIdxOrPtr(GetActorEyesViewPointFn) / 8;

	void (*GetActorEyesViewPointOriginal)(AActor* Actor, FVector* OutLocation, FRotator* OutRotation) = decltype(GetActorEyesViewPointOriginal)(PlayerController->VFTable[GetActorEyesViewPointIndex]);
	return GetActorEyesViewPointOriginal(PlayerController, &Location, &Rotation); */

	if (auto MyFortPawn = PlayerController->GetMyFortPawn())
	{
		Location = MyFortPawn->GetActorLocation();
		Rotation = PlayerController->GetControlRotation();
		return;
	}

	return AFortPlayerControllerAthena::GetPlayerViewPointOriginal(PlayerController, Location, Rotation);
}

void AFortPlayerControllerAthena::ServerReadyToStartMatchHook(AFortPlayerControllerAthena* PlayerController)
{
	LOG_INFO(LogDev, "ServerReadyToStartMatch!");

	if (Fortnite_Version <= 2.5) // techinally we should do this at the end of OnReadyToStartMatch
	{
		static auto QuickBarsOffset = PlayerController->GetOffset("QuickBars", false);

		if (QuickBarsOffset != -1)
		{
			auto& QuickBars = PlayerController->Get<AActor*>(QuickBarsOffset);

			// LOG_INFO(LogDev, "QuickBarsOld: {}", __int64(QuickBars));

			if (QuickBars)
				return ServerReadyToStartMatchOriginal(PlayerController);

			static auto FortQuickBarsClass = FindObject<UClass>("/Script/FortniteGame.FortQuickBars");

			QuickBars = GetWorld()->SpawnActor<AActor>(FortQuickBarsClass);

			// LOG_INFO(LogDev, "QuickBarsNew: {}", __int64(QuickBars));

			if (!QuickBars)
				return ServerReadyToStartMatchOriginal(PlayerController);

			PlayerController->Get<AActor*>(QuickBarsOffset)->SetOwner(PlayerController);
		}
	}

	return ServerReadyToStartMatchOriginal(PlayerController);
}


void AFortPlayerControllerAthena::UpdateTrackedAttributesHook(AFortPlayerControllerAthena* PlayerController)
{
	LOG_INFO(LogDev, "UpdateTrackedAttributesHook Return: 0x{:x}", __int64(_ReturnAddress()) - __int64(GetModuleHandleW(0)));

	// IDK IF GADGET IS A PARAM OR WHAT

	auto PlayerState = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState()); // really we only need zone

	if (!PlayerState)
		return;

	auto ASC = PlayerState->GetAbilitySystemComponent();

	if (!ASC)
		return;

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	auto& ItemInstances = WorldInventory->GetItemList().GetItemInstances();

	std::vector<UFortItem*> ItemInstancesToRemove;

	for (int i = 0; i < ItemInstances.Num(); ++i)
	{
		auto ItemInstance = ItemInstances.at(i);
		auto GadgetItemDefinition = Cast<UFortGadgetItemDefinition>(ItemInstance->GetItemEntry()->GetItemDefinition());

		if (!GadgetItemDefinition)
			continue;

		if (!GadgetItemDefinition->ShouldDestroyGadgetWhenTrackedAttributesIsZero())
			continue;

		bool bIsTrackedAttributesZero = true;

		for (int i = 0; i < GadgetItemDefinition->GetTrackedAttributes().Num(); ++i)
		{
			auto& CurrentTrackedAttribute = GadgetItemDefinition->GetTrackedAttributes().at(i);

			int CurrentAttributeValue = -1;

			for (int i = 0; i < ASC->GetSpawnedAttributes().Num(); ++i)
			{
				auto CurrentSpawnedAttribute = ASC->GetSpawnedAttributes().at(i);

				if (CurrentSpawnedAttribute->IsA(CurrentTrackedAttribute.AttributeOwner))
				{
					auto PropertyOffset = CurrentSpawnedAttribute->GetOffset(CurrentTrackedAttribute.GetAttributePropertyName());

					if (PropertyOffset != -1)
					{
						if (CurrentSpawnedAttribute->GetPtr<FFortGameplayAttributeData>(PropertyOffset)->GetCurrentValue() > 0)
						{
							bIsTrackedAttributesZero = false;
							break; // hm
						}
					}
				}
			}
		}

		if (bIsTrackedAttributesZero)
		{
			ItemInstancesToRemove.push_back(ItemInstance);
		}
	}

	for (auto ItemInstanceToRemove : ItemInstancesToRemove)
	{
		auto GadgetItemDefinition = Cast<UFortGadgetItemDefinition>(ItemInstanceToRemove->GetItemEntry()->GetItemDefinition());

		WorldInventory->RemoveItem(ItemInstanceToRemove->GetItemEntry()->GetItemGuid(), nullptr, ItemInstanceToRemove->GetItemEntry()->GetCount(), true);

		static auto MulticastTriggerOnGadgetTrackedAttributeDestroyedFXFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerStateZone.MulticastTriggerOnGadgetTrackedAttributeDestroyedFX");
		PlayerState->ProcessEvent(MulticastTriggerOnGadgetTrackedAttributeDestroyedFXFn, &GadgetItemDefinition);
	}

	if (ItemInstancesToRemove.size() > 0)
		WorldInventory->Update();
}

void AFortPlayerControllerAthena::ServerClientIsReadyToRespawnHook(AFortPlayerControllerAthena* PlayerControllerAthena)
{
	AFortPlayerStateAthena* PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerControllerAthena->GetPlayerState());
	AFortGameModeAthena* GameModeAthena = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

	if (!PlayerStateAthena || !GameModeAthena)
		return;

	AFortGameStateAthena* GameStateAthena = Cast<AFortGameStateAthena>(GameModeAthena->GetGameState());
	if (!GameStateAthena) return;

	if (GameStateAthena->IsRespawningAllowed(PlayerStateAthena))
	{
		FFortRespawnData* RespawnData = PlayerStateAthena->GetRespawnData();

		if (RespawnData->IsServerReady() && RespawnData->IsRespawnDataAvailable())
		{
			const FVector& RespawnLocation = RespawnData->GetRespawnLocation();
			const FRotator& RespawnRotation = RespawnData->GetRespawnRotation();

			// RestartPlayer doesn't work, idk why
			static auto SpawnDefaultPawnAtTransformFn = FindObject<UFunction>(L"/Script/Engine.GameModeBase.SpawnDefaultPawnAtTransform");

			FTransform SpawnTransform{};
			SpawnTransform.Translation = RespawnLocation;
			SpawnTransform.Rotation = RespawnRotation.Quaternion();
			SpawnTransform.Scale3D = FVector(1, 1, 1);

			struct { AController* NewPlayer; FTransform SpawnTransform; APawn* ReturnValue; }
			AGameModeBase_SpawnDefaultPawnAtTransform_Params{ PlayerControllerAthena, SpawnTransform };

			GameModeAthena->ProcessEvent(SpawnDefaultPawnAtTransformFn, &AGameModeBase_SpawnDefaultPawnAtTransform_Params);

			AFortPlayerPawn* PlayerPawn = Cast<AFortPlayerPawn>(AGameModeBase_SpawnDefaultPawnAtTransform_Params.ReturnValue);

			if (!PlayerPawn)
				return;

			PlayerPawn->SetOwner(PlayerControllerAthena);

			PlayerControllerAthena->Possess(PlayerPawn);

			PlayerPawn->SetMaxHealth(100);
			PlayerPawn->SetHealth(100);
			PlayerPawn->SetMaxShield(100);
			PlayerPawn->SetShield(100);

			PlayerControllerAthena->RespawnPlayerAfterDeath(true);

			RespawnData->IsClientReady() = true;
		}
	}

	printf_s(__FUNCTION__"\n");
}