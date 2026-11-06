#include "FactoryFPVPanelViewportLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVPanel.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVHangarViewportRenderTest,
    "FactoryFPV.Presentation.HangarViewportDPI",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVHangarViewportRenderTest::RunTest(const FString&) {
    if (!FApp::CanEverRender()) {
        AddInfo(TEXT("Production viewport/DPI rendering requires RHI; skipped with NullRHI."));return true;
    }
    TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>,const FString&)> FindButton;
    FindButton=[&](TSharedRef<SWidget> Widget,const FString& Text)->TSharedPtr<SButton> {
        auto* Children=Widget->GetChildren();
        if (Widget->GetTypeAsString()==TEXT("SButton") && Children->Num()==1 &&
            Children->GetChildAt(0)->GetTypeAsString()==TEXT("STextBlock") &&
            StaticCastSharedRef<STextBlock>(Children->GetChildAt(0))->GetText().ToString()==Text)
            return StaticCastSharedRef<SButton>(Widget);
        for (int32 Index=0;Index<Children->Num();++Index)
            if (auto Button=FindButton(Children->GetChildAt(Index),Text)) return Button;
        return nullptr;
    };
    struct FCase { FIntPoint Pixels; float PreferenceScale; };
    const FCase Cases[]{{FIntPoint(1280,720),1.f},{FIntPoint(1920,1080),1.f},{FIntPoint(2560,1080),1.f},
        {FIntPoint(1280,720),.75f},{FIntPoint(1280,720),1.25f}};
    const TCHAR* Pages[]{TEXT("Radio"),TEXT("Flight"),TEXT("Racing")};
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FactoryFPV");
    IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(true);
    for (const auto& Case:Cases) {
        // This is the actual resolution curve around the production wrapper,
        // equivalent to SGameLayerManager's viewport DPIScaler, not a panel in
        // an unscaled test window. Preference variants never change game config.
        const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Case.Pixels)*Case.PreferenceScale;
        auto Panel=SNew(SFactoryFPVPanel);
        auto View=SNew(SDPIScaler).DPIScale(Dpi)[FactoryFPVPanelViewportLayout::Make(Panel)];
        const FVector2D Size(Case.Pixels.X,Case.Pixels.Y);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,false);
        if (!TestNotNull(TEXT("Production hangar viewport render target"),Target)) return false;
        for (const TCHAR* Page:Pages) {
            const auto Tab=FindButton(Panel,Page);
            if (!TestNotNull(TEXT("Actual production tab button"),Tab.Get())) return false;
            Tab->SimulateClick();
            Renderer.DrawWidget(Target,View,Size,0.f,false);FlushRenderingCommands();
            Renderer.DrawWidget(Target,View,Size,1.f/60.f,false);FlushRenderingCommands();
            const auto& Geometry=Panel->GetCachedGeometry();
            const FVector2D TopLeft=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
            const FVector2D BottomRight=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
            TestTrue(TEXT("Production DPI panel remains inside the inset physical viewport"),
                TopLeft.X>=8 && TopLeft.Y>=8 && BottomRight.X<=Size.X-8 && BottomRight.Y<=Size.Y-8);
            TestTrue(TEXT("Actual modal uses enough physical height for legible settings"),BottomRight.Y-TopLeft.Y>=Size.Y*.85);
            TestTrue(TEXT("Body fonts retain at least 85 percent of their design paint scale"),
                Geometry.GetAccumulatedLayoutTransform().GetScale()>=.85f);
            for (const TCHAR* Footer:{TEXT("Save settings"),TEXT("Save & fly")}) {
                const auto Button=FindButton(Panel,Footer);
                if (!TestNotNull(TEXT("Fixed production footer button"),Button.Get())) return false;
                const auto& FooterGeometry=Button->GetCachedGeometry();
                const FVector2D A=FooterGeometry.LocalToAbsolute(FVector2D::ZeroVector);
                const FVector2D B=FooterGeometry.LocalToAbsolute(FooterGeometry.GetLocalSize());
                TestTrue(TEXT("Full actionable footer stays inside the panel after native DPI"),
                    A.X>=TopLeft.X-1 && A.Y>=TopLeft.Y-1 && B.X<=BottomRight.X+1 && B.Y<=BottomRight.Y+1 && B.Y-A.Y>=15);
            }
            TArray<FColor> Pixels;
            FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            if (!TestTrue(TEXT("Read actual production wrapper pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))) return false;
            TestEqual(TEXT("Production DPI viewport pixel count"),Pixels.Num(),Case.Pixels.X*Case.Pixels.Y);
            TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Case.Pixels.X,Case.Pixels.Y,Pixels,Png);
            const FString Path=Directory/FString::Printf(TEXT("HangarViewport-%s-%dx%d-Preference%.2f.png"),Page,Case.Pixels.X,Case.Pixels.Y,Case.PreferenceScale);
            TestTrue(TEXT("Save production DPI layout evidence"),FFileHelper::SaveArrayToFile(Png,*Path));
            AddInfo(FString::Printf(TEXT("%s / native DPI %.3f / physical panel scale %.3f"),*Path,Dpi,Geometry.GetAccumulatedLayoutTransform().GetScale()));
        }
    }
    return true;
}
#endif
