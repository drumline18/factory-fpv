#pragma once

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"

namespace FactoryFPVPanelViewportLayout {

// The native viewport has already applied its resolution/UI DPI scale. Give
// the modal the entire inset viewport so it can grow as well as shrink; keeping
// a fixed-size, DownOnly child here makes 720p body text about six pixels tall.
// Global game DPI and saved UI preferences remain owned by the game. This
// fixed-aspect modal fits the available window; its exterior margin follows DPI.
inline TSharedRef<SBorder> Make(TSharedRef<SWidget> Content,const FPointerEventHandler& MouseDown=FPointerEventHandler()) {
    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.f,0.f,0.f,.72f))
        .Padding(24.f)
        .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
        .OnMouseButtonDown(MouseDown)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)[Content]];
}

}
