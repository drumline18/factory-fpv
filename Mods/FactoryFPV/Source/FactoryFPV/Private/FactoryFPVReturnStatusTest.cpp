#include "FactoryFPVReturnStatusWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPanel.h"
#include "Components/BoxComponent.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SDPIScaler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVReturnStatusTest,
    "FactoryFPV.Interface.PioneerReturnStatus",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVReturnStatusTest::RunTest(const FString&) {
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindTag;
    FindTag=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget> {
        if (Widget->GetTag()==Tag) return Widget;
        auto* Children=Widget->GetChildren();
        for (int32 I=0;I<Children->Num();++I) if (auto Found=FindTag(Children->GetChildAt(I),Tag)) return Found;
        return nullptr;
    };
    TFunction<FString(TSharedRef<SWidget>)> Text;
    Text=[&](TSharedRef<SWidget> Widget) {
        FString Result;
        if (Widget->GetTypeAsString()==TEXT("STextBlock")) Result=StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
        auto* Children=Widget->GetChildren();
        for (int32 I=0;I<Children->Num();++I) Result+=TEXT(" ")+Text(Children->GetChildAt(I));
        return Result;
    };
    // Drive only the presentation attribute for loading, which the SDK's
    // non-partitioned fixture world cannot naturally report.
    auto DisplayedStatus=EFactoryFPVReturnStatus::LoadingGround;
    auto HangarCard=FactoryFPVReturnStatusWidget::MakeHangarCard(TAttribute<EFactoryFPVReturnStatus>::CreateLambda([&] { return DisplayedStatus; }),
        FOnClicked::CreateLambda([] { return FReply::Handled(); }));
    auto HangarCardHost=SNew(SBox)[HangarCard];
    auto TestRecoveryButton=FindTag(HangarCard,TEXT("FactoryFPVRecoverPioneer"));
    if (!TestNotNull(TEXT("Status-bound hangar recovery action"),TestRecoveryButton.Get())) return false;
    auto RefreshHangarCard=[&] { HangarCardHost->Invalidate(EInvalidateWidgetReason::Layout);HangarCardHost->SlatePrepass(); };
    RefreshHangarCard();
    TestTrue(TEXT("Hangar shows automatic waiting guidance while ground loads"),HangarCard->GetVisibility().IsVisible() &&
        Text(HangarCard).Contains(TEXT("is loading")) && Text(HangarCard).Contains(TEXT("resumes automatically")));
    TestFalse(TEXT("Hangar disables recovery while ground loads"),TestRecoveryButton->IsEnabled());
    DisplayedStatus=EFactoryFPVReturnStatus::MissingFloor;RefreshHangarCard();
    TestTrue(TEXT("The same hangar card enables recovery when loaded original support is unavailable"),TestRecoveryButton->IsEnabled() &&
        Text(HangarCard).Contains(TEXT("is unavailable")) && Text(HangarCard).Contains(TEXT("6 m sideways and 30 m below")));
    DisplayedStatus=EFactoryFPVReturnStatus::None;RefreshHangarCard();
    TestEqual(TEXT("The same hangar card collapses after return"),HangarCard->GetVisibility().ToString(),EVisibility::Collapsed.ToString());

    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Return-status world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    auto ViewportOverlay=SNew(SOverlay);Viewport->SetViewportOverlayWidget(nullptr,ViewportOverlay);
    GEngine->GetWorldContextFromWorld(World)->GameViewport=Viewport;
    auto* WorldProperty=FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("World"));
    if (!TestNotNull(TEXT("Native viewport world binding"),WorldProperty)) return false;
    WorldProperty->SetObjectPropertyValue_InContainer(Viewport,World);
    auto* Player=NewObject<ULocalPlayer>(GEngine);Player->ViewportClient=Viewport;
    auto* PC=World->SpawnActor<APlayerController>();PC->SetPlayer(Player);
    auto* Character=World->SpawnActor<ACharacter>(FVector(0,0,89),FRotator::ZeroRotator);
    PC->SetPawn(Character);PC->SetViewTarget(Character);
    auto* Movement=Character->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;Movement->SetMovementMode(MOVE_Walking);
    auto* Floor=World->SpawnActor<AActor>();
    auto* FloorBody=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(FloorBody);Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(10000,10000,50));FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);FloorBody->SetCollisionResponseToAllChannels(ECR_Block);FloorBody->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,-50));
    auto* Hangar=NewObject<UFactoryFPVLocalPlayerSubsystem>(Player);
    if (!TestTrue(TEXT("Return-status fixture binds real subsystem controls"),Hangar->BindController(PC,Viewport))) return false;
    ON_SCOPE_EXIT { Hangar->UnbindController(); };
    const int32 Baseline=ViewportOverlay->GetChildren()->Num();
    Hangar->UpdateReturnStatus();
    TestFalse(TEXT("Ordinary walking never creates a return banner"),Hangar->ReturnStatusOverlay.IsValid());
    auto* Anchor=World->SpawnActor<AFactoryFPVPioneerAnchor>();FString Error;
    if (!TestTrue(TEXT("Fixture anchors an actually grounded character"),Anchor->Hold(Character,Error))) { AddError(Error);return false; }
    TestTrue(TEXT("Normal active hold reports no pending-return status"),Anchor->GetReturnStatus()==EFactoryFPVReturnStatus::None);
    Hangar->UpdateReturnStatus();
    TestFalse(TEXT("An active-flight hold is not a pending-return banner"),Hangar->ReturnStatusOverlay.IsValid());
    FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);Anchor->RequestRelease();
    if (!TestTrue(TEXT("Actual missing support leaves a live pending return"),Anchor->IsReleasePending() && Anchor->GetReturnStatus()==EFactoryFPVReturnStatus::MissingFloor)) return false;
    // The same production helper is called by Tick. The SDK's nonvirtual game
    // health query is a false stub, so use a real engine Character and invoke
    // this UI update directly without pretending to qualify native health/WP.
    Hangar->UpdateReturnStatus();
    if (!TestTrue(TEXT("Pending return mounts a real viewport overlay"),Hangar->ReturnStatusOverlay.IsValid() && ViewportOverlay->GetChildren()->Num()==Baseline+1)) return false;
    auto Banner=Hangar->ReturnStatusOverlay.ToSharedRef();
    // A bound SWidget visibility is cached until Slate's layout update. Run
    // prepass on the mounted parent, as the viewport does before first paint
    // and hit-test-grid population, rather than inspecting an unlaid-out child.
    ViewportOverlay->Invalidate(EInvalidateWidgetReason::Layout);
    ViewportOverlay->SlatePrepass();
    auto ReturnCard=FindTag(Banner,TEXT("FactoryFPVReturnStatus"));
    if (!TestNotNull(TEXT("Actual visible card has its stable status tag"),ReturnCard.Get())) return false;
    TestEqual(TEXT("Return banner cannot take mouse input"),Banner->GetVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
    TestEqual(TEXT("Tagged card and its children cannot take mouse input"),ReturnCard->GetVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
    TestTrue(TEXT("Missing-floor guidance names the actual problem and F6 recovery"),Text(ReturnCard.ToSharedRef()).Contains(TEXT("original floor is unavailable")) && Text(ReturnCard.ToSharedRef()).Contains(TEXT("F6")));
    TestFalse(TEXT("Noninteractive banner adds no move/look holds"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    TestEqual(TEXT("Banner never releases the pioneer's safety hold"),Movement->MovementMode,MOVE_None);

    if (FApp::CanEverRender()) {
        FWidgetRenderer Renderer(true);
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("FactoryFPV");IFileManager::Get().MakeDirectory(*Directory,true);
        for (const FIntPoint Pixels:{FIntPoint(1280,720),FIntPoint(1920,1080),FIntPoint(2560,1080)}) {
            for (const auto Status:{EFactoryFPVReturnStatus::MissingFloor,EFactoryFPVReturnStatus::LoadingGround}) {
                const TCHAR* Name=Status==EFactoryFPVReturnStatus::MissingFloor ? TEXT("MissingFloor") : TEXT("LoadingGround");
                // MissingFloor uses the real mounted widget above. Loading's
                // presentation uses the identical production widget factory;
                // this does not fabricate World Partition completion state.
                auto Content=Status==EFactoryFPVReturnStatus::MissingFloor ? StaticCastSharedRef<SWidget>(ViewportOverlay) : FactoryFPVReturnStatusWidget::Make(Status,EVisibility::HitTestInvisible);
                auto View=SNew(SDPIScaler).DPIScale(GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Pixels))[Content];
                const FVector2D Size(Pixels.X,Pixels.Y);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,false);
                if (!TestNotNull(TEXT("Return-banner render target"),Target)) return false;
                Renderer.DrawWidget(Target,View,Size,0.f,false);FlushRenderingCommands();
                const auto FirstPaintRoot=Status==EFactoryFPVReturnStatus::MissingFloor ? Banner : Content;
                TestEqual(TEXT("First actual paint keeps the complete status overlay noninteractive"),FirstPaintRoot->GetVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
                TestFalse(TEXT("First painted status root accepts no pointer hits"),FirstPaintRoot->GetVisibility().IsHitTestVisible());
                TestFalse(TEXT("First painted status root admits no child pointer hits"),FirstPaintRoot->GetVisibility().AreChildrenHitTestVisible());
                Renderer.DrawWidget(Target,View,Size,1.f/60.f,false);FlushRenderingCommands();
                auto RenderCard=FindTag(Content,TEXT("FactoryFPVReturnStatus"));
                const auto& Geometry=RenderCard->GetCachedGeometry();
                const FVector2D A=Geometry.LocalToAbsolute(FVector2D::ZeroVector),B=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
                TestTrue(TEXT("Whole status card fits inside the physical viewport"),A.X>=8 && A.Y>=8 && B.X<=Size.X-8 && B.Y<=Size.Y-8 && B.Y>A.Y);
                TestTrue(TEXT("Body text remains at least 15 physical pixels at 720p"),Geometry.GetAccumulatedLayoutTransform().GetScale()*24.f>=15.f);
                const FString RenderedText=Text(RenderCard.ToSharedRef());
                TestTrue(TEXT("Rendered return guidance gives the action available in its current state"),
                    Status==EFactoryFPVReturnStatus::MissingFloor ? RenderedText.Contains(TEXT("F6")) :
                    RenderedText.Contains(TEXT("resumes automatically")) && !RenderedText.Contains(TEXT("F6")));
                TestTrue(TEXT("Loading and missing-floor copy remain distinct"),Text(RenderCard.ToSharedRef()).Contains(Status==EFactoryFPVReturnStatus::LoadingGround ? TEXT("is loading") : TEXT("is unavailable")));
                TArray<FColor> Image;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
                if (!TestTrue(TEXT("Read actual banner pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Image,Flags))) return false;
                int32 Bright=0;
                for (int32 Y=FMath::Clamp(FMath::FloorToInt(A.Y),0,Pixels.Y-1);Y<FMath::Clamp(FMath::CeilToInt(B.Y),0,Pixels.Y);++Y)
                    for (int32 X=FMath::Clamp(FMath::FloorToInt(A.X),0,Pixels.X-1);X<FMath::Clamp(FMath::CeilToInt(B.X),0,Pixels.X);++X) {
                        const auto& Pixel=Image[Y*Pixels.X+X];if (Pixel.R>80 || Pixel.G>80 || Pixel.B>80) ++Bright;
                    }
                TestTrue(TEXT("Actual banner render contains visible text"),Bright>500);
                TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Pixels.X,Pixels.Y,Image,Png);
                const FString Path=Directory/FString::Printf(TEXT("ReturnStatus-%s-%dx%d.png"),Name,Pixels.X,Pixels.Y);
                TestTrue(TEXT("Save actual return-status render evidence"),FFileHelper::SaveArrayToFile(Png,*Path));
            }
        }
    } else AddInfo(TEXT("Return-status render skipped without RHI; native anchor/UI lifecycle still tested."));

    auto RefreshVisibility=[&] { Banner->Invalidate(EInvalidateWidgetReason::Layout);Banner->SlatePrepass();return Banner->GetVisibility(); };
    auto* Console=NewObject<UConsole>(Viewport);Viewport->ViewportConsole=Console;Console->ConsoleState=TEXT("Typing");
    TestEqual(TEXT("Actual console ownership hides return guidance"),RefreshVisibility().ToString(),EVisibility::Collapsed.ToString());
    Console->ConsoleState=NAME_None;
    TestEqual(TEXT("Console close restores pending-return guidance"),RefreshVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
    Viewport->ViewportConsole=nullptr;
    Hangar->OpenPanel();
    TestEqual(TEXT("Hangar hides the banner behind its actionable recovery card"),RefreshVisibility().ToString(),EVisibility::Collapsed.ToString());
    auto Recover=FindTag(Hangar->Panel.ToSharedRef(),TEXT("FactoryFPVRecoverPioneer"));
    if (!TestNotNull(TEXT("Actual hangar recovery action is tagged"),Recover.Get()) || !TestTrue(TEXT("Recovery tag identifies the real SButton"),Recover->GetTypeAsString()==TEXT("SButton"))) return false;
    ViewportOverlay->SlatePrepass();
    TestTrue(TEXT("Actual missing original floor enables the hangar recovery action"),Recover->IsEnabled());
    StaticCastSharedPtr<SButton>(Recover)->SimulateClick();
    TestTrue(TEXT("A failed actual recovery action keeps the pioneer safe and explains retry"),Anchor->IsReleasePending() && Text(Hangar->Panel.ToSharedRef()).Contains(TEXT("No clear loaded walkable ground")));
    Hangar->ClosePanel();
    TestEqual(TEXT("Closing hangar resumes the noninteractive pending guidance"),RefreshVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
    Hangar->UnbindController();
    TestFalse(TEXT("Unbind clears the old controller's banner"),Hangar->ReturnStatusOverlay.IsValid());
    TestEqual(TEXT("Unbind removes every owned viewport widget"),ViewportOverlay->GetChildren()->Num(),0);
    auto* Other=World->SpawnActor<APlayerController>();Other->SetPlayer(Player);
    Other->SetPawn(World->SpawnActor<ACharacter>(FVector(2000,0,89),FRotator::ZeroRotator));
    TestTrue(TEXT("New controller binds independently"),Hangar->BindController(Other,Viewport));Hangar->UpdateReturnStatus();
    TestFalse(TEXT("New controller cannot inherit another pioneer's return banner"),Hangar->ReturnStatusOverlay.IsValid());
    Hangar->UnbindController();PC->SetPlayer(Player);
    TestTrue(TEXT("Original controller can resume its genuine pending return"),Hangar->BindController(PC,Viewport));Hangar->UpdateReturnStatus();
    TestTrue(TEXT("Original pioneer's still-pending hold remains discoverable"),Hangar->ReturnStatusOverlay.IsValid());
    Hangar->OpenPanel();Recover=FindTag(Hangar->Panel.ToSharedRef(),TEXT("FactoryFPVRecoverPioneer"));
    if (!TestNotNull(TEXT("Reopened hangar retains its actual recovery action"),Recover.Get())) return false;
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Anchor->Tick(0.f);
    TestTrue(TEXT("Actual pioneer return automatically releases onto restored support"),Movement->MovementMode==MOVE_Walking && !Anchor->IsReleasePending());
    TestTrue(TEXT("Completed recovery reports no pending-return status"),Anchor->GetReturnStatus()==EFactoryFPVReturnStatus::None);
    Hangar->UpdateReturnStatus();
    TestFalse(TEXT("Completed return removes its status widget"),Hangar->ReturnStatusOverlay.IsValid());Hangar->ClosePanel();
    TestEqual(TEXT("Completed return leaves the original widget baseline"),ViewportOverlay->GetChildren()->Num(),Baseline);
    auto* LastAnchor=World->SpawnActor<AFactoryFPVPioneerAnchor>();
    if (!TestTrue(TEXT("Actor-loss fixture starts with a genuine grounded hold"),LastAnchor->Hold(Character,Error))) return false;
    FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);LastAnchor->RequestRelease();Hangar->UpdateReturnStatus();
    TestTrue(TEXT("Actor-loss fixture mounts pending guidance"),Hangar->ReturnStatusOverlay.IsValid());
    Character->Destroy();Hangar->UpdateReturnStatus();
    TestFalse(TEXT("Destroyed pioneer cannot leave stale return guidance"),Hangar->ReturnStatusOverlay.IsValid());
    return true;
}
#endif
