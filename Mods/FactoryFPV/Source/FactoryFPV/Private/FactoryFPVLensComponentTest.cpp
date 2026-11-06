#include "FactoryFPVLensComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVLensIsolationTest,"FactoryFPV.Camera.LensIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVLensIsolationTest::RunTest(const FString&) {
    auto* Lens=NewObject<UFactoryFPVLensComponent>();
    auto* Camera=NewObject<UCameraComponent>();
    auto* PioneerCamera=NewObject<UCameraComponent>();
    auto* OtherBlendable=NewObject<UMaterialInstanceDynamic>();
    Camera->PostProcessSettings.AddBlendable(OtherBlendable,.35f);
    const auto Original=Camera->PostProcessSettings.WeightedBlendables.Array;
    TestTrue(TEXT("Off succeeds without altering the camera"),Lens->ApplyToCamera(Camera,0));
    TestEqual(TEXT("Off adds no fullscreen pass"),Camera->PostProcessSettings.WeightedBlendables.Array.Num(),Original.Num());
    TestNotNull(TEXT("Cooked lens asset is referenced"),Lens->LensBase.Get());
    TestTrue(TEXT("Enabling the lens attaches it to the FPV camera"),Lens->ApplyToCamera(Camera,.65f));
    TestEqual(TEXT("Exactly one lens blendable is added"),Camera->PostProcessSettings.WeightedBlendables.Array.Num(),Original.Num()+1);
    TestEqual(TEXT("Pioneer camera remains unchanged"),PioneerCamera->PostProcessSettings.WeightedBlendables.Array.Num(),0);
    TestEqual(TEXT("Camera blend weight is preserved"),Camera->PostProcessBlendWeight,1.f);
    float Value=0;
    if (Lens->LensMaterial) Lens->LensMaterial->GetScalarParameterValue(TEXT("LensStrength"),Value);
    TestEqual(TEXT("Material receives the requested lens strength"),Value,.65f);
    Lens->ApplyToCamera(Camera,.25f);
    TestEqual(TEXT("Changing strength does not stack passes"),Camera->PostProcessSettings.WeightedBlendables.Array.Num(),Original.Num()+1);
    Lens->ApplyToCamera(Camera,std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("NaN disables only this effect"),Camera->PostProcessSettings.WeightedBlendables.Array.Num(),Original.Num());
    TestTrue(TEXT("Unrelated blendable and its weight survive"),Camera->PostProcessSettings.WeightedBlendables.Array[0].Object==OtherBlendable && Camera->PostProcessSettings.WeightedBlendables.Array[0].Weight==.35f);
    Lens->ApplyToCamera(Camera,10);
    Lens->LensMaterial->GetScalarParameterValue(TEXT("LensStrength"),Value);
    TestEqual(TEXT("Runtime strength is bounded too"),Value,1.f);
    Lens->ApplyToCamera(PioneerCamera,.3f); // Explicit reassignment cleans up the previous target.
    TestEqual(TEXT("Reassignment removes lens from the old target"),Camera->PostProcessSettings.WeightedBlendables.Array.Num(),Original.Num());
    Lens->Clear();
    TestEqual(TEXT("Clear removes the explicitly assigned effect"),PioneerCamera->PostProcessSettings.WeightedBlendables.Array.Num(),0);
    return true;
}

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVLensPixelTest,"FactoryFPV.Camera.LensCheckerGridPixels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVLensPixelTest::RunTest(const FString&) {
    if (!FApp::CanEverRender()) {
        AddInfo(TEXT("Lens pixel test requires -RenderOffscreen; skipped under NullRHI."));
        return true;
    }
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Lens pixel test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { FlushRenderingCommands();World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };

    // Transient test geometry/material. No extra scene capture or test-grid
    // asset is included in the released mod; the actual runtime uses its camera.
    auto* Grid=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    Grid->SetShadingModel(MSM_Unlit);Grid->TwoSided=true;
    auto* Coordinate=NewObject<UMaterialExpressionTextureCoordinate>(Grid);
    auto* Pattern=NewObject<UMaterialExpressionCustom>(Grid);
    Pattern->OutputType=CMOT_Float3;
    Pattern->Code=TEXT("float2 cell = abs(frac(UV * 24.0 + 0.5) - 0.5); float gridMask = 1.0 - step(0.04, min(cell.x, cell.y)); return lerp(float3(0.08,0.14,0.23),float3(0.85,0.90,0.98),gridMask);");
    FCustomInput CoordinateInput;CoordinateInput.InputName=TEXT("UV");CoordinateInput.Input.Connect(0,Coordinate);
    Pattern->Inputs.Add(CoordinateInput);
    Grid->GetExpressionCollection().AddExpression(Coordinate);
    Grid->GetExpressionCollection().AddExpression(Pattern);
    Grid->GetEditorOnlyData()->EmissiveColor.Connect(0,Pattern);
    Grid->PostEditChange();
    Grid->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* LensBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/FactoryFPV/Materials/M_FPVLens.M_FPVLens"));
    if (!TestNotNull(TEXT("Grid plane"),Plane) || !TestNotNull(TEXT("Real lens material"),LensBase)) return false;
    LensBase->GetMaterial()->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
    // Compare the material's zero branch against an engine texture-copy pass
    // with the same post-process pipeline. No-pass tonemap output can use a
    // different intermediate format/resolve; runtime off bypass is tested below.
    auto* ReferenceBase=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    ReferenceBase->MaterialDomain=MD_PostProcess;
    ReferenceBase->BlendableLocation=LensBase->GetMaterial()->BlendableLocation;
    ReferenceBase->BlendablePriority=LensBase->GetMaterial()->BlendablePriority;
    ReferenceBase->bIsBlendable=true;
    auto* ReferenceInput=NewObject<UMaterialExpressionSceneTexture>(ReferenceBase);
    ReferenceInput->SceneTextureId=PPI_PostProcessInput0;
    ReferenceBase->GetExpressionCollection().AddExpression(ReferenceInput);
    ReferenceBase->GetEditorOnlyData()->EmissiveColor.Connect(0,ReferenceInput);
    ReferenceBase->PostEditChange();
    ReferenceBase->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
    FAssetCompilingManager::Get().FinishAllCompilation();
    // FAssetCompilingManager does not own shader jobs. A non-null resource
    // with no reported errors can still render the default post-process material.
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    const auto* LensResource=LensBase->GetMaterialResource(GMaxRHIFeatureLevel);
    const auto* GridResource=Grid->GetMaterialResource(GMaxRHIFeatureLevel);
    const auto* ReferenceResource=ReferenceBase->GetMaterialResource(GMaxRHIFeatureLevel);
    if (!TestNotNull(TEXT("Lens shader resource"),LensResource) || !TestNotNull(TEXT("Grid shader resource"),GridResource)) return false;
    if (!TestNotNull(TEXT("Identity reference shader resource"),ReferenceResource) ||
        !TestTrue(TEXT("Identity reference shader map is ready"),ReferenceResource->IsGameThreadShaderMapComplete()) ||
        !TestEqual(TEXT("Identity reference compiles without fallback"),ReferenceResource->GetCompileErrors().Num(),0)) return false;
    if (!TestTrue(TEXT("Actual lens shader map is ready, without a fallback"),LensResource->IsGameThreadShaderMapComplete())) return false;
    if (!TestEqual(TEXT("Real lens shader compiles without errors"),LensResource->GetCompileErrors().Num(),0) ||
        !TestEqual(TEXT("Source grid shader compiles without fallback"),GridResource->GetCompileErrors().Num(),0)) return false;

    auto* Wall=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UStaticMeshComponent>(Wall);
    Wall->SetRootComponent(Mesh);Wall->AddInstanceComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetStaticMesh(Plane);Mesh->SetMaterial(0,Grid);Mesh->SetCastShadow(false);Mesh->RegisterComponent();
    Wall->SetActorTransform(FTransform(FRotator(90,0,0),FVector(300,0,0),FVector(12,12,1)));
    auto* ViewActor=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(ViewActor);
    ViewActor->SetRootComponent(Capture);ViewActor->AddInstanceComponent(Capture);
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    // UMaterialInterface::OverrideBlendableSettings requires View.State. A
    // manually triggered capture otherwise skips every post-process material.
    Capture->bAlwaysPersistRenderingState=true;Capture->bUseRayTracingIfEnabled=false;
    Capture->CaptureSource=SCS_FinalColorLDR;Capture->FOVAngle=90;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActorComponents(Wall);
    Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetBloom(false);Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetPostProcessing(true);Capture->ShowFlags.SetPostProcessMaterial(true);Capture->ShowFlags.SetTonemapper(true);
    Capture->RegisterComponent();
    if (!TestNotNull(TEXT("Persistent view state permits real post-process blendables"),Capture->GetViewState(0))) return false;
    auto* Camera=NewObject<UCameraComponent>(ViewActor);
    auto* Lens=NewObject<UFactoryFPVLensComponent>(ViewActor);
    Camera->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Camera->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;Camera->PostProcessSettings.AutoExposureBias=0;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FactoryFPV");
    IFileManager::Get().MakeDirectory(*Directory,true);
    AddInfo(TEXT("Material zero is compared with an engine PostProcessInput0 copy at the same blend location (<=1 target quantization); component off is compared with the original no-pass image and must restore exact pixels. Each sample starts with identical view/frame state."));
    for (const FIntPoint Size:{FIntPoint(640,360),FIntPoint(800,340)}) {
        auto* Target=NewObject<UTextureRenderTarget2D>(ViewActor);
        Target->ClearColor=FLinearColor(1,0,1,1);
        Target->InitCustomFormat(Size.X,Size.Y,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);
        Capture->TextureTarget=Target;
        auto Snapshot=[&](const FString& Name,TArray<FColor>& Pixels) {
            // Give each controlled sample the same initial view state/frame
            // index. Otherwise tonemap quantization noise changes across captures.
            Capture->bAlwaysPersistRenderingState=false;Capture->GetViewState(0);
            Capture->bAlwaysPersistRenderingState=true;
            Capture->bCameraCutThisFrame=true;
            Capture->PostProcessSettings=Camera->PostProcessSettings;Capture->PostProcessBlendWeight=1;
            Capture->CaptureScene();FlushRenderingCommands();
            Capture->CaptureScene();FlushRenderingCommands();
            FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
            TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
            const FString Path=Directory/FString::Printf(TEXT("Lens-%s-%dx%d.png"),*Name,Size.X,Size.Y);
            if (!FFileHelper::SaveArrayToFile(Png,*Path)) return false;
            AddInfo(Path);return true;
        };
        TArray<FColor> Off,Reference,Identity,Fisheye,Restored;
        Lens->ApplyToCamera(Camera,0);
        if (!TestTrue(TEXT("Capture unmodified checker grid"),Snapshot(TEXT("Off"),Off))) return false;
        int SourceMinimum=255,SourceMaximum=0;
        for (const auto& Pixel:Off) {
            SourceMinimum=FMath::Min(SourceMinimum,int(Pixel.R));
            SourceMaximum=FMath::Max(SourceMaximum,int(Pixel.R));
        }
        if (!TestTrue(TEXT("Unmodified source contains a visible contrasting grid"),SourceMaximum>100 && SourceMaximum-SourceMinimum>60)) return false;
        auto* ReferenceMaterial=UMaterialInstanceDynamic::Create(ReferenceBase,ViewActor);
        Camera->PostProcessSettings.AddBlendable(ReferenceMaterial,1);
        if (!TestTrue(TEXT("Capture engine identity reference pass"),Snapshot(TEXT("Reference"),Reference))) return false;
        Camera->PostProcessSettings.RemoveBlendable(ReferenceMaterial);
        auto* IdentityMaterial=UMaterialInstanceDynamic::Create(LensBase,ViewActor);
        IdentityMaterial->SetScalarParameterValue(TEXT("LensStrength"),0);
        Camera->PostProcessSettings.AddBlendable(IdentityMaterial,1);
        if (!TestTrue(TEXT("Capture actual zero-strength shader"),Snapshot(TEXT("Identity"),Identity))) return false;
        Camera->PostProcessSettings.RemoveBlendable(IdentityMaterial);
        Lens->ApplyToCamera(Camera,.8f);
        if (!TestTrue(TEXT("Capture actual barrel-distorted grid"),Snapshot(TEXT("Fisheye"),Fisheye))) return false;
        Lens->ApplyToCamera(Camera,0);
        if (!TestTrue(TEXT("Capture view after disabling lens"),Snapshot(TEXT("Restored"),Restored))) return false;
        int Changed=0,MaximumIdentityDelta=0,MaximumRestoreDelta=0;
        for (int Index=0;Index<Off.Num();++Index) {
            auto Difference=[](const FColor& A,const FColor& B) { return FMath::Max3(FMath::Abs(int(A.R)-B.R),FMath::Abs(int(A.G)-B.G),FMath::Abs(int(A.B)-B.B)); };
            MaximumIdentityDelta=FMath::Max(MaximumIdentityDelta,Difference(Reference[Index],Identity[Index]));
            MaximumRestoreDelta=FMath::Max(MaximumRestoreDelta,Difference(Off[Index],Restored[Index]));
            if (Difference(Off[Index],Fisheye[Index])>8) ++Changed;
        }
        TestTrue(TEXT("Zero-strength material is an identity (at most target quantization)"),MaximumIdentityDelta<=1);
        AddInfo(FString::Printf(TEXT("Lens %dx%d: identity max delta %d; restored max delta %d; visibly changed pixels %d/%d"),Size.X,Size.Y,MaximumIdentityDelta,MaximumRestoreDelta,Changed,Off.Num()));
        TestEqual(TEXT("Disabling the lens restores exact original pixels"),MaximumRestoreDelta,0);
        TestTrue(TEXT("Lens visibly bends the grid rather than silently doing nothing"),Changed>Off.Num()/10);
        auto OuterGridLine=[&](int Y) {
            int TotalX=0,Count=0;
            for (int X=Size.X*8/10;X<Size.X*9/10;++X) {
                if (Fisheye[Y*Size.X+X].R>190) { TotalX+=X;++Count; }
            }
            // Reject a missing stripe or a horizontal line filling the window.
            return Count>0 && Count<Size.X/40 ? double(TotalX)/Count : -1.;
        };
        const double MiddleStripe=OuterGridLine(Size.Y/2+12);
        const double UpperStripe=OuterGridLine(Size.Y/4+7);
        TestTrue(TEXT("Outer vertical stripe has actual outward barrel curvature"),MiddleStripe>=0 && UpperStripe>=0 && MiddleStripe>UpperStripe+2.);
        const int Center=Size.Y/2*Size.X+Size.X/2;
        TestTrue(TEXT("Lens keeps the optical center fixed"),FMath::Abs(int(Off[Center].R)-Fisheye[Center].R)<=3);
        int DarkBorder=0;
        for (int X=0;X<Size.X;++X) for (const int Y:{0,Size.Y-1}) {
            const auto Pixel=Fisheye[Y*Size.X+X];if (int(Pixel.R)+Pixel.G+Pixel.B<6) ++DarkBorder;
        }
        for (int Y=0;Y<Size.Y;++Y) for (const int X:{0,Size.X-1}) {
            const auto Pixel=Fisheye[Y*Size.X+X];if (int(Pixel.R)+Pixel.G+Pixel.B<6) ++DarkBorder;
        }
        TestEqual(TEXT("Barrel mapping creates no black edge holes"),DarkBorder,0);
    }
    return true;
}
#endif
#endif
