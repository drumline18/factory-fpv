#pragma once
#include "UI/FGGameUI.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UnrealType.h"

// Shared observed native splash lifecycle, independent of front-end-only flags.
namespace FactoryFPVShippingStartup {
inline bool Ready(UFGGameUI* UI,FString& Reason,bool& Observed,bool& AnimationObserved,bool& Finished,bool& FrontEndFinished) {
    Finished=false;FrontEndFinished=UI && UI->IsFrontEndLoadingScreenFinished();
    if (!UI) { Reason=TEXT("Waiting for actual GameUI.");return false; }
    const auto* Field=FindFProperty<FObjectPropertyBase>(UI->GetClass(),TEXT("mFicsitLogoSplashWidget"));
    const FString Expected=TEXT("/Game/FactoryGame/Interface/UI/InGame/Widget_FicsitLogoSplash.Widget_FicsitLogoSplash_C");
    if (!Field || !Field->PropertyClass || Field->PropertyClass->GetPathName()!=Expected) {
        Reason=TEXT("Known HUD startup-splash schema is unavailable; readiness cannot be inferred.");return false;
    }
    auto* Object=Field->GetObjectPropertyValue_InContainer(UI);
    if (!Object) {
        if (!FrontEndFinished && !Observed) { Reason=TEXT("Waiting to observe actual direct-map startup splash lifecycle.");return false; }
        Finished=true;return true;
    }
    auto* Splash=Cast<UUserWidget>(Object);
    if (Splash && Splash->GetClass()->GetPathName()==Expected) Observed=true;
    const auto* AnimationField=Splash ? FindFProperty<FObjectPropertyBase>(Splash->GetClass(),TEXT("SpawnAnim")) : nullptr;
    auto* Animation=AnimationField && AnimationField->PropertyClass && AnimationField->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass()) ?
        Cast<UWidgetAnimation>(AnimationField->GetObjectPropertyValue_InContainer(Splash)) : nullptr;
    if (!Splash || Splash->GetClass()->GetPathName()!=Expected || !Animation) { Reason=TEXT("Waiting for known startup splash/animation initialization.");return false; }
    const bool Playing=Splash->IsAnimationPlaying(Animation);
    if (Playing) AnimationObserved=true;
    bool Mounted=Splash->IsInViewport(),Visible=Splash->IsVisible();
    float Opacity=Splash->GetRenderOpacity()*Splash->GetColorAndOpacity().A;
    auto Slate=Splash->GetCachedWidget();
    if (Slate && FSlateApplication::IsInitialized()) {
        Mounted|=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef()).IsValid();
        for (int32 Depth=0;Slate && Depth<128;++Depth) { Visible&=Slate->GetVisibility().IsVisible();Opacity*=Slate->GetRenderOpacity();Slate=Slate->GetParentWidget(); }
        if (Slate) { Reason=TEXT("Startup splash ancestry is unresolved.");return false; }
    }
    if (Playing || (!AnimationObserved && Mounted && Visible && (!FMath::IsFinite(Opacity) || Opacity>0.f))) {
        Reason=TEXT("Waiting for actual FICSIT/welcome splash presentation to finish.");return false;
    }
    // In direct-map Shipping startup the front-end flag remains false and the
    // root Canvas stays opaque. Completion is the actual known animation's
    // witnessed playing -> stopped transition, as qualified by the UI probe.
    Finished=true;return true;
}
}
