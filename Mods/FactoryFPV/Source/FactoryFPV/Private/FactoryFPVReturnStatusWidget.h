#pragma once
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVSettings.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace FactoryFPVReturnStatusWidget {
inline FText Message(EFactoryFPVReturnStatus Status) {
    if (Status==EFactoryFPVReturnStatus::LoadingGround)
        return FText::FromString(TEXT("Ground around your pioneer is loading. Movement stays held while it loads and resumes automatically if the original floor is available."));
    if (Status==EFactoryFPVReturnStatus::MissingFloor)
        return FText::FromString(FString::Printf(
            TEXT("The original floor is unavailable. Movement stays held. Press %s, then Recover pioneer to search nearby safe ground."),
            *FFactoryFPVKeyboardShortcuts::KeyLabel(GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar)));
    return FText::GetEmpty();
}

inline TSharedRef<SWidget> MakeHangarCard(TAttribute<EFactoryFPVReturnStatus> Status,const FOnClicked& OnRecover) {
    return SNew(SBox).Tag(FName(TEXT("FactoryFPVHangarReturnStatus")))
        .Visibility_Lambda([Status] { return Status.Get()==EFactoryFPVReturnStatus::None ? EVisibility::Collapsed : EVisibility::Visible; })
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(.18f,.07f,.01f)).Padding(10)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(STextBlock).Text_Lambda([Status] {
                    const auto Current=Status.Get();
                    if (Current==EFactoryFPVReturnStatus::MissingFloor)
                        return FText::FromString(TEXT("The original floor is unavailable. Recover pioneer searches verified, loaded support within 6 m sideways and 30 m below."));
                    return Message(Current);
                }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).AutoWrapText(true) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRecoverPioneer"))).Text(FText::FromString(TEXT("Recover pioneer")))
                    .IsEnabled_Lambda([Status] { return Status.Get()==EFactoryFPVReturnStatus::MissingFloor; })
                    .OnClicked(OnRecover) ] ] ];
}

inline TSharedRef<SWidget> Make(TAttribute<EFactoryFPVReturnStatus> Status,TAttribute<EVisibility> Visibility) {
    // Match the FPV view's physical viewport scaling rather than inheriting a
    // tiny modal font from the game's DPI multiplier. No global setting changes.
    return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both).Visibility(Visibility)
        [ SNew(SBox).WidthOverride(1920.f).HeightOverride(1080.f)
            [ SNew(SOverlay)
                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f,48.f,0.f,0.f)
                [ SNew(SBox).WidthOverride(1140.f)
                    [ SNew(SBorder).Tag(FName(TEXT("FactoryFPVReturnStatus"))).Visibility(EVisibility::HitTestInvisible)
                        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                        .BorderBackgroundColor(FLinearColor(.08f,.045f,.015f,.97f)).Padding(18.f)
                        [ SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(FText::FromString(TEXT("FACTORY FPV  /  PIONEER RETURN")))
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20)).ColorAndOpacity(FLinearColor(1,.46f,.12f)) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f,0.f,0.f)
                            [ SNew(STextBlock).Text_Lambda([Status] { return Message(Status.Get()); }).AutoWrapText(true)
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),24)).ColorAndOpacity(FLinearColor(1,.88f,.68f)) ] ] ] ] ] ];
}
}
