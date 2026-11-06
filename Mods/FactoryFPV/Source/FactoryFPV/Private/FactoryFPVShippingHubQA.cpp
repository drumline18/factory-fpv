#include "FactoryFPVShippingHubQA.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVShippingHubPayment.h"
#include "FactoryFPVShippingStartup.h"
#include "Async/Async.h"
#include "Buildables/FGBuildableHubTerminal.h"
#include "Buildables/FGBuildableTradingPost.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGGameState.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "FGRecipeManager.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "FGSchematicManager.h"
#include "FGWorkBench.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace FactoryFPVShippingHubQAInternal {
constexpr double TimeLimit=65.;
constexpr float ExpectedCooldown=60.f;
constexpr float CooldownRoundingTolerance=.001f;
const TCHAR* CloneName=TEXT("FPV-QA-Clone");
const TCHAR* ProgressionSaveName=TEXT("FPV-QA-Progression");

FString NormalizedPath(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool GuardPaths(FString& NativeSaveDirectory,FString& Reason) {
    const TCHAR* CommandLine=FCommandLine::Get();
    if (!FParse::Param(CommandLine,TEXT("FactoryFPVAutomation")) ||
        !FParse::Param(CommandLine,TEXT("RenderOffScreen")) ||
        !FParse::Param(CommandLine,TEXT("NoAutoSave")) ||
        !FParse::Param(CommandLine,TEXT("SavesUseProjectSavedDir"))) {
        Reason=TEXT("HUB QA requires -FactoryFPVAutomation, -RenderOffScreen, -NoAutoSave and -SavesUseProjectSavedDir.");
        return false;
    }
    const FString Saved=NormalizedPath(FPaths::ProjectSavedDir());
    const FString Run=FPaths::GetPath(Saved);
    const FString RunName=FPaths::GetCleanFilename(Run);
    const FString Prefix=TEXT("shipping-world-qa-");
    const FString Native=UFGSaveSystem::GetSaveDirectoryPath();
    NativeSaveDirectory=Native.IsEmpty() ? FString() : NormalizedPath(Native);
    FGuid RunId;
    if (!FPaths::GetCleanFilename(Saved).Equals(TEXT("Saved"),ESearchCase::IgnoreCase) ||
        !FPaths::GetCleanFilename(FPaths::GetPath(Run)).Equals(TEXT("build"),ESearchCase::IgnoreCase) ||
        !RunName.StartsWith(Prefix,ESearchCase::IgnoreCase) ||
        !FGuid::ParseExact(RunName.Mid(Prefix.Len()),EGuidFormats::Digits,RunId) || !RunId.IsValid() ||
        !NativeSaveDirectory.StartsWith(Saved+TEXT("/"),ESearchCase::IgnoreCase)) {
        Reason=TEXT("HUB QA requires a fresh build/shipping-world-qa-<guid>/Saved directory with the real save directory under it.");
        return false;
    }
    return true;
}

bool ResolveClonePath(const UWorld& World,const FString& NativeSaveDirectory,FString& ClonePath) {
    if (!UFGSaveSystem::GetAbsolutePathForSaveGame(&World,CloneName,ClonePath)) return false;
    ClonePath=NormalizedPath(ClonePath);
    return ClonePath.StartsWith(NativeSaveDirectory+TEXT("/"),ESearchCase::IgnoreCase) &&
        FPaths::GetCleanFilename(ClonePath).Equals(TEXT("FPV-QA-Clone.sav"),ESearchCase::CaseSensitive) &&
        IFileManager::Get().FileExists(*ClonePath);
}

FString Escape(FString Text) {
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}

const TCHAR* JsonBool(bool Value) { return Value ? TEXT("true") : TEXT("false"); }

void Marker(bool bProgression,const TCHAR* Result,const FString& Reason,const FString& Evidence=FString(),bool bSavedWorld=false) {
    const TCHAR* Stage=bProgression ? TEXT("progression-unlock") : TEXT("hub");
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\",\"injectedControls\":false,\"savedWorld\":%s,\"seededIngredients\":false,\"directGrant\":false%s}"),
        Stage,Result,*Escape(Reason),JsonBool(bSavedWorld),*Evidence);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}

template <typename T>
bool AddedOnly(const TArray<TSubclassOf<T>>& Before,const TArray<TSubclassOf<T>>& After,TSubclassOf<T> Target) {
    if (Before.Contains(Target) || After.Num()!=Before.Num()+1) return false;
    TMap<UClass*,int32> Remaining;
    for (const auto& Entry : Before) ++Remaining.FindOrAdd(Entry.Get());
    ++Remaining.FindOrAdd(Target.Get());
    for (const auto& Entry : After) {
        int32* Count=Remaining.Find(Entry.Get());
        if (!Count || *Count<=0) return false;
        --*Count;
    }
    return true;
}

bool VerifyOriginalCost(AFGSchematicManager& Manager,TSubclassOf<UFGSchematic> Schematic) {
    const auto Cost=UFGSchematic::GetCost(Schematic);
    const auto Remaining=Manager.GetRemainingCostFor(Schematic);
    if (Cost.Num()!=3 || Remaining.Num()!=Cost.Num()) return false;
    for (const auto& Entry : Cost) {
        if (!Entry.ItemClass || Entry.Amount<=0 || !Remaining.ContainsByPredicate([&](const FItemAmount& Item) {
            return Item.ItemClass==Entry.ItemClass && Item.Amount==Entry.Amount;
        })) return false;
    }
    const auto Paid=Manager.GetPaidOffCostFor(Schematic);
    return !Manager.IsSchematicPaidOff(Schematic) && !Paid.ContainsByPredicate([](const FItemAmount& Item) { return Item.Amount!=0; });
}

bool IsLiveBuildable(const AFGBuildable& Buildable,const UWorld& World) {
    return Buildable.GetWorld()==&World && Buildable.HasActorBegunPlay() && Buildable.HasAuthority() &&
        !Buildable.IsActorBeingDestroyed() && !Buildable.GetIsDismantled() && !Buildable.IsBuildableInsideBlueprintDesigner();
}

AFGBuildableHubTerminal* FindCompletedHub(UWorld& World,AFGBuildableTradingPost*& Owner,FString& Details) {
    // The parent-to-terminal reference is native SaveGame state. The terminal's
    // reverse convenience pointer is not serialized and can be null after load.
    int32 Hubs=0,TerminalActors=0,LiveTerminalActors=0,ForwardLinks=0,ReverseLinks=0,Completed=0;
    FString LastCandidate;
    TArray<AFGBuildableHubTerminal*> Terminals;
    for (TActorIterator<AFGBuildableHubTerminal> It(&World);It;++It) {
        auto* Terminal=*It;
        ++TerminalActors;
        if (IsValid(Terminal) && IsLiveBuildable(*Terminal,World)) {
            ++LiveTerminalActors;
            Terminals.Add(Terminal);
            if (IsValid(Terminal->GetTradingPost())) ++ReverseLinks;
        }
    }
    for (TActorIterator<AFGBuildableTradingPost> It(&World);It;++It) {
        auto* Hub=*It;
        ++Hubs;
        if (!IsValid(Hub)) continue;
        const bool bHubLive=IsLiveBuildable(*Hub,World);
        const bool bComplete=Hub->IsTradingPostComplete();
        if (bComplete) ++Completed;
        auto* Terminal=Hub->mHubTerminal.Get();
        if (IsValid(Terminal)) ++ForwardLinks;
        else {
            const auto* Reverse=Terminals.FindByPredicate([Hub](AFGBuildableHubTerminal* Candidate) { return Candidate->GetTradingPost()==Hub; });
            Terminal=Reverse ? *Reverse : nullptr;
        }
        const bool bTerminalLive=IsValid(Terminal) && IsLiveBuildable(*Terminal,World);
        const bool bConflictingLink=bTerminalLive && IsValid(Terminal->GetTradingPost()) && Terminal->GetTradingPost()!=Hub;
        LastCandidate=FString::Printf(TEXT("level=%d hubLive=%d complete=%d buildEffect=%d terminalLive=%d conflictingLink=%d"),
            Hub->GetTradingPostLevel(),bHubLive,bComplete,Hub->IsPlayingBuildEffect(),bTerminalLive,bConflictingLink);
        if (bHubLive && bComplete && !Hub->IsPlayingBuildEffect() && bTerminalLive && !bConflictingLink) {
            Owner=Hub;
            return Terminal;
        }
    }
    Details=FString::Printf(TEXT("loaded HUB actors=%d complete=%d terminal actors=%d live terminals=%d forward links=%d reverse links=%d; last candidate: %s"),
        Hubs,Completed,TerminalActors,LiveTerminalActors,ForwardLinks,ReverseLinks,*LastCandidate);
    return nullptr;
}

struct FRun : TSharedFromThis<FRun> {
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UFGSaveSession> ProgressionSession;
    TArray<FactoryFPVShippingHubPayment::FPayment> Payments;
    FString HubPath,TerminalPath;
    FString ProgressionSavePath,ProgressionMapName,ProgressionSessionName,ProgressionSaveError;
    FString Reason=TEXT("Waiting for the living pioneer in the disposable HUB save.");
    double Deadline=FPlatformTime::Seconds()+TimeLimit;
    float ShipSecondsRemaining=0.f;
    int32 ProgressionSaveCallbacks=0;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bHubVerified=false,bTierOneAvailable=false,bUnpaidBefore=false,bRecipeAbsentBefore=false;
    bool bActiveSelectionVerified=false,bExactPaymentVerified=false,bPaidOffBeforeLaunch=false,bNativeLaunchVerified=false;
    bool bOnlyTargetPurchased=false,bRecipeUnlocked=false,bShipCooldownVerified=false;
    bool bProgression=false,bProgressionSaveRequested=false,bProgressionSaveCallbackSucceeded=false,bProgressionSaveVerified=false;

    FString Evidence() const {
        TArray<FString> Rows;
        for (const auto& Row : Payments) {
            Rows.Add(FString::Printf(TEXT("{\"item\":\"%s\",\"expected\":%d,\"inventoryDelta\":%d,\"depotDelta\":%d,\"paidDelta\":%d}"),
                *Escape(GetPathNameSafe(Row.Item.Get())),Row.Expected,Row.InventoryDelta,Row.DepotDelta,Row.PaidDelta));
        }
        FString Result=FString::Printf(TEXT(",\"hubVerified\":%s,\"tierOneAvailable\":%s,\"unpaidBefore\":%s,\"recipeAbsentBefore\":%s,\"activeSelectionVerified\":%s,\"exactPaymentVerified\":%s,\"paidOffBeforeLaunch\":%s,\"nativeLaunchVerified\":%s,\"onlyTargetPurchased\":%s,\"recipeUnlocked\":%s,\"shipCooldownVerified\":%s,\"schematic\":\"%s\",\"recipe\":\"%s\",\"hub\":\"%s\",\"terminal\":\"%s\",\"shipSecondsRemaining\":%.6f,\"payments\":[%s]"),
            JsonBool(bHubVerified),JsonBool(bTierOneAvailable),JsonBool(bUnpaidBefore),JsonBool(bRecipeAbsentBefore),
            JsonBool(bActiveSelectionVerified),JsonBool(bExactPaymentVerified),JsonBool(bPaidOffBeforeLaunch),JsonBool(bNativeLaunchVerified),
            JsonBool(bOnlyTargetPurchased),JsonBool(bRecipeUnlocked),JsonBool(bShipCooldownVerified),
            *Escape(UFactoryFPVRadioSchematic::StaticClass()->GetPathName()),*Escape(UFactoryFPVRadioRecipe::StaticClass()->GetPathName()),
            *Escape(HubPath),*Escape(TerminalPath),FMath::IsFinite(ShipSecondsRemaining) ? ShipSecondsRemaining : 0.f,*FString::Join(Rows,TEXT(",")));
        if (bProgression) {
            Result+=FString::Printf(TEXT(",\"progressionSaveRequested\":%s,\"progressionSaveCallbackSucceeded\":%s,\"progressionSaveVerified\":%s,\"progressionSaveName\":\"%s\",\"progressionSavePath\":\"%s\""),
                JsonBool(bProgressionSaveRequested),JsonBool(ProgressionSaveCallbacks==1 && bProgressionSaveCallbackSucceeded),
                JsonBool(bProgressionSaveVerified),ProgressionSaveName,*Escape(ProgressionSavePath));
        }
        return Result;
    }

    bool Finish(bool Success,const FString& Message) const {
        Marker(bProgression,Success ? TEXT("PASS") : TEXT("FAIL"),Message,Evidence(),bProgressionSaveVerified);
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
        return false;
    }

    bool Ready(UWorld& Current,AFGPlayerController*& PC,AFGCharacterPlayer*& Pioneer,const FString& NativeSaveDirectory) {
        auto* Mode=Current.GetAuthGameMode<AFGGameMode>();
        auto* Save=Mode ? Mode->GetSaveSession() : nullptr;
        PC=Cast<AFGPlayerController>(Current.GetFirstPlayerController());
        Pioneer=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Mode || Mode->IsMainMenuGameMode() || !Current.HasBegunPlay() || !Save || Save->HasSaveGameOperationPending() ||
            !PC || !PC->GetLocalPlayer() || !PC->HasAuthority() || PC->IsPaused() || !Pioneer || !Pioneer->HasAuthority() ||
            !Pioneer->IsAliveAndWell() || PC->GetViewTarget()!=Pioneer) return false;
        const auto& Header=Save->GetLoadedSaveHeader();
        const bool bRequestedClone=Mode->GetLoadData().LoadName.Equals(CloneName,ESearchCase::CaseSensitive);
        if (Header.SaveName.IsEmpty() || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (!bRequestedClone && (bProgression || !Header.SaveName.Equals(CloneName,ESearchCase::CaseSensitive)))) {
            Reason=TEXT("The loaded native save identity is not FPV-QA-Clone.");
            return false;
        }
        FString Clone;
        if (!ResolveClonePath(Current,NativeSaveDirectory,Clone)) {
            Reason=TEXT("The game has not resolved the exact disposable FPV-QA-Clone save under its native save directory.");
            return false;
        }
        if (!FactoryFPVShippingStartup::Ready(PC->GetGameUI(),Reason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished))
            return false;
        if (bProgression) {
            ProgressionSession=Save;
            ProgressionMapName=Header.MapName;
            ProgressionSessionName=Header.SessionName;
        }
        return true;
    }

    bool CheckFreshProgressionSlot(UWorld& Current,const FString& NativeSaveDirectory) {
        FString ClonePath;
        if (!ResolveClonePath(Current,NativeSaveDirectory,ClonePath)) {
            Reason=TEXT("Cannot resolve the original progression clone.");
            return false;
        }
        const FString Expected=FPaths::Combine(FPaths::GetPath(ClonePath),FString(ProgressionSaveName)+TEXT(".sav"));
        const FString Output=NormalizedPath(UFGSaveSystem::CreateAbsolutePath(&Current,ProgressionSaveName,false));
        if (Output!=Expected || (!ProgressionSavePath.IsEmpty() && Output!=ProgressionSavePath)) {
            Reason=FString::Printf(TEXT("The progression save must target the exact approved slot beside the disposable clone (resolved=%s expected=%s)."),*Output,*Expected);
            return false;
        }
        const auto* System=UFGSaveSystem::Get(&Current);
        FString Resolved;
        if (!System || IFileManager::Get().FileExists(*Output) || IFileManager::Get().DirectoryExists(*Output) ||
            UFGSaveSystem::GetAbsolutePathForSaveGame(&Current,ProgressionSaveName,Resolved) ||
            System->SaveGameExistsSync(ProgressionSaveName) || System->GetCachedSaves().ContainsByPredicate([](const FSaveHeader& Header) {
                return Header.SaveName.Equals(ProgressionSaveName,ESearchCase::IgnoreCase);
            })) {
            Reason=TEXT("The progression slot is occupied or its native save system is unavailable.");
            return false;
        }
        ProgressionSavePath=Output;
        return true;
    }

    bool RequestProgressionSave(UWorld& Current,const FString& NativeSaveDirectory) {
        const auto* Mode=Current.GetAuthGameMode<AFGGameMode>();
        auto* Save=ProgressionSession.Get();
        if (!Mode || !Save || Mode->GetSaveSession()!=Save || Save->HasSaveGameOperationPending())
            return Finish(false,TEXT("The original native save session is unavailable or busy."));
        if (!CheckFreshProgressionSlot(Current,NativeSaveDirectory)) return Finish(false,Reason);
        bProgressionSaveRequested=true;
        Reason=TEXT("Waiting for the native progression save callback.");
        const TWeakPtr<FRun> Weak=AsShared();
        Save->SaveGame(ProgressionSaveName,FOnSaveGameComplete::CreateLambda([Weak](bool Success,const FText& Error,void*) {
            const FString Message=Error.ToString();
            auto Complete=[Weak,Success,Message] {
                if (auto Run=Weak.Pin()) {
                    ++Run->ProgressionSaveCallbacks;
                    Run->bProgressionSaveCallbackSucceeded=Success;
                    Run->ProgressionSaveError=Message;
                }
            };
            if (IsInGameThread()) Complete();
            else AsyncTask(ENamedThreads::GameThread,MoveTemp(Complete));
        }),nullptr);
        return true;
    }

    bool TickProgressionSave(UWorld& Current) {
        const auto* Mode=Current.GetAuthGameMode<AFGGameMode>();
        const auto* Save=ProgressionSession.Get();
        if (!Mode || !Save || Mode->GetSaveSession()!=Save)
            return Finish(false,TEXT("The original native save session ended during the progression save."));
        if (ProgressionSaveCallbacks>1 || (ProgressionSaveCallbacks==1 && !bProgressionSaveCallbackSucceeded))
            return Finish(false,TEXT("The progression save did not complete with one successful native callback: ")+ProgressionSaveError);
        if (ProgressionSaveCallbacks!=1 || Save->HasSaveGameOperationPending()) return true;

        const auto* System=UFGSaveSystem::Get(&Current);
        FSaveHeader Header;
        FString Actual;
        if (!System || IFileManager::Get().FileSize(*ProgressionSavePath)<=0 ||
            !UFGSaveSystem::GetAbsolutePathForSaveGame(&Current,ProgressionSaveName,Actual) || NormalizedPath(Actual)!=ProgressionSavePath ||
            !System->LoadSaveGameHeaderSync(ProgressionSaveName,Header) ||
            !Header.SaveName.Equals(ProgressionSaveName,ESearchCase::CaseSensitive) ||
            !Header.MapName.Equals(ProgressionMapName,ESearchCase::CaseSensitive) ||
            !Header.SessionName.Equals(ProgressionSessionName,ESearchCase::CaseSensitive)) {
            Reason=TEXT("Waiting for the completed native progression file to resolve with the exact slot, map and session.");
            return true;
        }
        bProgressionSaveVerified=true;
        return Finish(true,TEXT("Native HUB payment and launch purchased only Remote Factory Reconnaissance, unlocked its Workshop recipe and saved the resulting world to the exact progression slot."));
    }

    bool Tick() {
        if (FPlatformTime::Seconds()>Deadline) return Finish(false,Reason);
        FString SaveDirectory;
        if (!GuardPaths(SaveDirectory,Reason)) return Finish(false,Reason);
        auto* Current=World.Get();
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone)
            return Finish(false,TEXT("The disposable standalone HUB world was unloaded."));
        if (bProgressionSaveRequested) return TickProgressionSave(*Current);
        AFGPlayerController* PC=nullptr;
        AFGCharacterPlayer* Pioneer=nullptr;
        if (!Ready(*Current,PC,Pioneer,SaveDirectory)) return true;

        const TSubclassOf<UFGSchematic> Schematic=UFactoryFPVRadioSchematic::StaticClass();
        const TSubclassOf<UFGRecipe> Recipe=UFactoryFPVRadioRecipe::StaticClass();
        auto* Manager=AFGSchematicManager::Get(Current);
        auto* Recipes=AFGRecipeManager::Get(Current);
        auto* State=Current->GetGameState<AFGGameState>();
        auto* Inventory=Pioneer->GetInventory();
        AFGBuildableTradingPost* Hub=nullptr;
        FString HubDetails;
        auto* Terminal=FindCompletedHub(*Current,Hub,HubDetails);
        auto* WorkshopClass=LoadClass<UFGWorkBench>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"));
        bHubVerified=Hub && Terminal;
        if (!Manager || !Recipes || !State || !Inventory || !Terminal || !WorkshopClass) {
            Reason=FString::Printf(TEXT("Waiting for paid HUB prerequisites (%s; schematic manager=%d recipe manager=%d game state=%d inventory=%d Workshop class=%d)."),
                *HubDetails,Manager!=nullptr,Recipes!=nullptr,State!=nullptr,Inventory!=nullptr,WorkshopClass!=nullptr);
            return true;
        }
        HubPath=Hub->GetPathName();
        TerminalPath=Terminal->GetPathName();
        if (!Manager->HasAuthority() || State->GetCheatNoCost() || State->GetCheatTurboBuildMode() || Inventory->GetNoBuildCost())
            return Finish(false,TEXT("HUB qualification requires normal paid gameplay on the authority."));

        TArray<TSubclassOf<UFGSchematic>> TierOne;
        Manager->GetHubSchematicsForTier(1,TierOne);
        bTierOneAvailable=TierOne.Contains(Schematic) && UFGSchematic::GetTechTier(Schematic)==1;
        if (!bTierOneAvailable) {
            Reason=TEXT("The native HUB Tier 1 list does not offer Remote Factory Reconnaissance.");
            return true;
        }
        TArray<TSubclassOf<UFGRecipe>> WorkshopBefore;
        Recipes->GetAvailableRecipesForProducer(WorkshopClass,WorkshopBefore);
        bRecipeAbsentBefore=!Recipes->IsRecipeAvailable(Recipe) && !WorkshopBefore.Contains(Recipe);
        bUnpaidBefore=!Manager->IsSchematicPurchased(Schematic,PC) && VerifyOriginalCost(*Manager,Schematic);
        if (!bUnpaidBefore || !bRecipeAbsentBefore)
            return Finish(false,TEXT("This clone must start with the radio milestone unpurchased and fully unpaid, and its Workshop recipe absent."));
        if (!Manager->IsShipAtTradingPost()) {
            Reason=TEXT("Waiting for the native freighter to return to the HUB before selecting or paying for the milestone.");
            return true;
        }
        if (UFGSchematic::GetTimeToComplete(Schematic)!=ExpectedCooldown)
            return Finish(false,TEXT("The radio milestone does not specify the expected 60-second ship cooldown."));
        if (bProgression && !CheckFreshProgressionSlot(*Current,SaveDirectory)) return Finish(false,Reason);

        TArray<TSubclassOf<UFGSchematic>> PurchasedBefore;
        Manager->GetAllPurchasedSchematics(PurchasedBefore);
        const auto RecipesBefore=Recipes->GetAllAvailableRecipes();
        bActiveSelectionVerified=Manager->CanSetAsActiveSchematic(Schematic) &&
            (Manager->GetActiveSchematic()==Schematic || Manager->SetActiveSchematic(Schematic)) && Manager->GetActiveSchematic()==Schematic;
        if (!bActiveSelectionVerified) return Finish(false,TEXT("The native HUB did not accept and retain selection of the target milestone."));
        FString PaymentError;
        bExactPaymentVerified=FactoryFPVShippingHubPayment::Pay(*PC,*Pioneer,*Manager,Schematic,Payments,PaymentError);
        if (!bExactPaymentVerified) return Finish(false,PaymentError);
        const auto Remaining=Manager->GetRemainingCostFor(Schematic);
        bPaidOffBeforeLaunch=Manager->GetActiveSchematic()==Schematic && Manager->IsSchematicPaidOff(Schematic) &&
            !Remaining.ContainsByPredicate([](const FItemAmount& Item) { return Item.Amount!=0; }) &&
            !Manager->IsSchematicPurchased(Schematic,PC) && !Recipes->IsRecipeAvailable(Recipe);
        if (!bPaidOffBeforeLaunch || !Manager->IsShipAtTradingPost())
            return Finish(false,TEXT("The active milestone was not exactly paid and awaiting launch of the at-home freighter."));

        Manager->LaunchShip(Pioneer);
        ShipSecondsRemaining=Manager->GetTimeUntilShipReturn();
        // The native end-time subtraction can report a few microseconds above 60.
        bShipCooldownVerified=!Manager->IsShipAtTradingPost() && FMath::IsFinite(ShipSecondsRemaining) &&
            ShipSecondsRemaining>0.f && ShipSecondsRemaining<=ExpectedCooldown+CooldownRoundingTolerance;
        bNativeLaunchVerified=bShipCooldownVerified && Manager->IsSchematicPurchased(Schematic,PC);
        TArray<TSubclassOf<UFGSchematic>> PurchasedAfter;
        Manager->GetAllPurchasedSchematics(PurchasedAfter);
        bOnlyTargetPurchased=AddedOnly(PurchasedBefore,PurchasedAfter,Schematic);
        TArray<TSubclassOf<UFGRecipe>> WorkshopAfter;
        Recipes->GetAvailableRecipesForProducer(WorkshopClass,WorkshopAfter);
        bRecipeUnlocked=Recipes->IsRecipeAvailable(Recipe) && AddedOnly(RecipesBefore,Recipes->GetAllAvailableRecipes(),Recipe) &&
            AddedOnly(WorkshopBefore,WorkshopAfter,Recipe);
        const bool Success=bNativeLaunchVerified && bOnlyTargetPurchased && bRecipeUnlocked;
        if (Success && bProgression) return RequestProgressionSave(*Current,SaveDirectory);
        return Finish(Success,Success ? TEXT("Native HUB payment and launch purchased only Remote Factory Reconnaissance, immediately unlocked its Workshop recipe and started the freighter cooldown.") :
            TEXT("Native launch did not produce the exact schematic, Workshop recipe and freighter transition."));
    }
};

TWeakPtr<FRun> Active;
}

void FactoryFPVShippingHubQA::Begin(UWorld* World,FOutputDevice& Output) {
    namespace Hub=FactoryFPVShippingHubQAInternal;
    FString ProgressionMode;
    const bool bProgressionArg=FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQAProgression="),ProgressionMode);
    const bool bProgression=ProgressionMode.Equals(TEXT("unlock"),ESearchCase::CaseSensitive);
    FString SaveDirectory,Reason;
    if (bProgressionArg && !bProgression) Reason=TEXT("HUB progression QA accepts only -FactoryFPVQAProgression=unlock.");
    if (!Reason.IsEmpty() || !Hub::GuardPaths(SaveDirectory,Reason) || !World || World->GetNetMode()!=NM_Standalone || Hub::Active.IsValid()) {
        if (Reason.IsEmpty()) Reason=TEXT("One standalone disposable HUB QA run is required.");
        Output.Log(*Reason);
        Hub::Marker(bProgression,TEXT("FAIL"),Reason);
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    auto Run=MakeShared<Hub::FRun>();
    Run->World=World;
    Run->bProgression=bProgression;
    Hub::Active=Run;
    Hub::Marker(bProgression,TEXT("PENDING"),TEXT("Waiting for the living pioneer, completed HUB and an unpaid native Tier 1 radio milestone."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.08f);
}
