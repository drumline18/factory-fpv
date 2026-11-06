#pragma once
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "UI/FGGameUI.h"

namespace FactoryFPVInteractionState {
inline bool IsNativePauseMenuOpen(const UFGGameUI* UI) {
    return UI && UI->IsPauseMenuOpen();
}
inline bool BlocksFlightOrPanel(UFGGameUI* UI) {
    // The native pause menu is separate from both the interact-widget stack
    // and world pause. It owns input even when either of those is clear.
    return IsNativePauseMenuOpen(UI) || (UI && UI->HasActiveInteractWidget());
}
inline bool BlocksPlayerInteraction(const APlayerController* Controller) {
    const auto* World=Controller ? Controller->GetWorld() : nullptr;
    if (!World || World->IsPaused() || Controller->IsPaused()) return true;
    const auto* GameController=Cast<AFGPlayerController>(Controller);
    return GameController && (GameController->IsPauseMenuOpen() || BlocksFlightOrPanel(GameController->GetGameUI()));
}
}
