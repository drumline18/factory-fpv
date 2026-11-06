#include "FactoryFPVShippingSimulationQA.h"

#include "FactoryFPVDrone.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVSettings.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Types/SlateAttributeMetaData.h"
#include "Widgets/Text/STextBlock.h"

namespace FactoryFPVShippingSimulationQAInternal {
constexpr double LiftColumnCm=1200.;
constexpr int32 MaxBatterySteps=1200;
constexpr int32 MaxImpactSteps=2400;
const TCHAR* Flag(bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }

bool HasVisibleText(const TSharedRef<SWidget>& Widget,const FString& Text) {
    // Scripted steps are synchronous. Evaluate the real visibility binding,
    // including previously collapsed readouts, before another Slate frame.
    FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(Widget.Get(),FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    if (!Widget->GetVisibility().IsVisible()) return false;
    if (Widget->GetTypeAsString()==TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()==Text) return true;
    auto* Children=Widget->GetChildren();
    for (int32 Index=0;Index<Children->Num();++Index)
        if (HasVisibleText(Children->GetChildAt(Index),Text)) return true;
    return false;
}
}

FString FFactoryFPVShippingSimulationEvidence::Json(bool bEnabled) const {
    using FactoryFPVShippingSimulationQAInternal::Flag;
    return FString::Printf(TEXT(",\"optionalSimulationEnabled\":%s,\"scriptedFlightSteps\":%s,"
        "\"simulationBatteryDrainVerified\":%s,\"simulationBatteryWarningsVerified\":%s,\"simulationBatteryCutoffVerified\":%s,"
        "\"simulationBatteryRecoveryVerified\":%s,\"simulationImpactVerified\":%s,\"simulationDamageRecoveryVerified\":%s,"
        "\"simulationSettingsRestored\":%s,\"simulationBatteryCutoffSteps\":%d,\"simulationModerateIntegrityPercent\":%.9f,"
        "\"simulationTerminalIntegrityPercent\":%.9f,\"simulationRechargePercent\":%.9f,\"simulationRepairPercent\":%.9f"),
        Flag(bEnabled),Flag(bEnabled),Flag(bBatteryDrain),Flag(bBatteryWarnings),Flag(bBatteryCutoff),Flag(bBatteryRecovery),Flag(bImpact),
        Flag(bDamageRecovery),Flag(bSettingsRestored),BatteryCutoffSteps,ModerateIntegrity,TerminalIntegrity,Recharge,Repair);
}

bool FFactoryFPVShippingSimulationQA::Run(AFactoryFPVDrone& Drone,UBoxComponent& OwnFloor,FInputActionUnifiedDelegate& RecoverKey,
    FFactoryFPVShippingSimulationEvidence& Evidence,FString& Error) {
    using namespace FactoryFPVShippingSimulationQAInternal;
    const auto Fail=[&Error](const FString& Reason) { Error=Reason;return false; };
    auto* World=Drone.GetWorld();auto* PC=Drone.Pilot.Get();
    const TCHAR* CommandLine=FCommandLine::Get();
    const bool bGuarded=FParse::Param(CommandLine,TEXT("FactoryFPVAutomation")) && FParse::Param(CommandLine,TEXT("FactoryFPVQAOptionalSimulation")) &&
        FParse::Param(CommandLine,TEXT("FactoryFPVQADroneRecovery")) && FParse::Param(CommandLine,TEXT("NoAutoSave")) &&
        FParse::Param(CommandLine,TEXT("RenderOffScreen"));
    if (!bGuarded || !World || World->GetNetMode()!=NM_Standalone || !OwnFloor.GetOwner() || !OwnFloor.GetOwner()->HasAnyFlags(RF_Transient) ||
        OwnFloor.GetWorld()!=World || !PC || PC->GetViewTarget()!=&Drone || !Drone.bSessionActive || Drone.InputMode!=TEXT("keyboard") ||
        !Drone.Overlay || !Drone.IsFlightOverlayVisible() || Drone.IsNativeUIBlockingFlight() || !Drone.Body || !Drone.Race ||
        Drone.Race->GetCourseId()!=0 || Drone.Flight.armed) return Fail(TEXT("Optional simulation requires a guarded, ordinary disarmed free-flight session with its visible OSD and owned transient support."));

    const auto* Committed=GetDefault<UFactoryFPVSettings>();
    auto* Settings=NewObject<UFactoryFPVSettings>(GetTransientPackage(),NAME_None,RF_Transient);
    Settings->ApplyTuning(Committed->CaptureTuning(FString()));
    Settings->MassKg=.65f;
    Settings->bAngleMode=false;
    Settings->bSimulateBattery=true;
    Settings->BatteryCells=1;
    Settings->BatteryCapacityMah=50.f;
    Settings->BatteryMaxPowerWatts=5000.f;
    Settings->bCrashDamageEnabled=false;
    Settings->ThrustToWeight=1.2f;
    Settings->Sanitize();
    Drone.ApplyFlightSettings(*Settings);
    const factoryfpv::Input Idle{0,0,0,0,true};
    bool bRestored=false;
    const auto Restore=[&] {
        if (bRestored) return;
        Drone.ApplyFlightSettings(*Committed);
        Drone.UpdateOsd(Idle);
        bRestored=true;
    };
    ON_SCOPE_EXIT { Restore(); };

    const FVector Launch=Drone.LaunchPosition;
    const FVector Upper=Launch+FVector(0,0,LiftColumnCm);
    const auto Shape=FCollisionShape::MakeSphere(Drone.Body->GetScaledSphereRadius());
    const auto Channel=Drone.Body->GetCollisionObjectType();
    const FCollisionResponseParams Responses(Drone.Body->GetCollisionResponseToChannels());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingOptionalSimulation),false,&Drone);
    Query.AddIgnoredActor(Drone.OriginalPawn.Get());
    FHitResult Upward,ExpectedLanding;
    if (!Drone.GetActorLocation().Equals(Launch,3.) || World->SweepSingleByChannel(Upward,Launch,Upper,FQuat::Identity,Channel,Shape,Query,Responses) ||
        !World->SweepSingleByChannel(ExpectedLanding,Upper,Launch-FVector(0,0,20),FQuat::Identity,Channel,Shape,Query,Responses) ||
        ExpectedLanding.bStartPenetrating || ExpectedLanding.GetComponent()!=&OwnFloor || ExpectedLanding.ImpactNormal.Z<.95) {
        return Fail(TEXT("Optional simulation requires a clear 12 m body-sweep column above the real supported launch pose."));
    }
    const auto ReadHud=[&](const factoryfpv::Input& Input,const FString& Text) {
        Drone.UpdateOsd(Input);
        return HasVisibleText(Drone.Overlay.ToSharedRef(),Text);
    };
    const auto BatteryLabel=[&] { return FString::Printf(TEXT("SIM BAT %.0f%% %.1fV"),Drone.Battery.remainingPercent(),Drone.Battery.packVoltage()); };
    const auto IntegrityLabel=[&] { return FString::Printf(TEXT("FRAME %.0f%%"),Drone.Damage.integrityPercent()); };
    const auto AtDisarmedLaunch=[&] {
        return Drone.bSessionActive && PC->GetViewTarget()==&Drone && !Drone.Flight.armed && Drone.Flight.thrust==0. &&
            Drone.GetActorLocation().Equals(Launch,.01) && FMath::IsNearlyEqual(Drone.Flight.position.z*100.,Launch.Z,.01);
    };

    const double FullVoltage=Drone.Battery.packVoltage();
    const double FullPercent=Drone.Battery.remainingPercent();
    const FString FullLabel=BatteryLabel();
    const bool bFullHud=ReadHud(Idle,FullLabel);
    const bool bArmed=Drone.TryArmFlight(Idle);
    if (!Drone.Battery.enabled() || Drone.Damage.enabled() || FullPercent!=100. || FullVoltage<=0. || !bFullHud || !bArmed) {
        return Fail(FString::Printf(TEXT("Fresh pack setup failed. Enabled=%d damage=%d cells=%d capacity=%.1f charge=%.6f V=%.6f HUD=%d label='%s' arm=%d."),
            Drone.Battery.enabled(),Drone.Damage.enabled(),Drone.Battery.parameters.cells,Drone.Battery.parameters.capacityMah,FullPercent,FullVoltage,bFullHud,*FullLabel,bArmed));
    }
    const factoryfpv::Input Powered{0,0,0,1,true};
    bool bLow=false,bCritical=false,bThrustSeen=false;
    double PreviousPercent=100.;
    for (int32 Step=1;Step<=MaxBatterySteps;++Step) {
        Drone.SimulateFlight(Powered,factoryfpv::StepSeconds);
        const double Percent=Drone.Battery.remainingPercent();
        if (!FMath::IsFinite(Percent) || Percent>PreviousPercent || Drone.GetActorLocation().Z>=Upper.Z)
            return Fail(TEXT("Battery script did not stay in the guarded column or reported nonmonotonic charge."));
        PreviousPercent=Percent;
        bThrustSeen|=Drone.Flight.armed && Drone.Flight.thrust>.1 && Percent<99. && Drone.Battery.packVoltage()<FullVoltage;
        if (!bLow && Drone.Battery.low() && !Drone.Battery.critical())
            bLow=ReadHud(Powered,BatteryLabel()) && ReadHud(Powered,TEXT("SIM BAT LOW - LAND SOON"));
        if (!bCritical && Drone.Battery.critical() && !Drone.Battery.empty())
            bCritical=ReadHud(Powered,BatteryLabel()) && ReadHud(Powered,TEXT("SIM BAT CRITICAL - LAND"));
        if (Drone.Battery.empty()) { Evidence.BatteryCutoffSteps=Step;break; }
        if (!Drone.Flight.armed) return Fail(TEXT("The powered battery probe disarmed before empty-pack cutoff."));
    }
    Evidence.bBatteryDrain=bThrustSeen && PreviousPercent==0.;
    Evidence.bBatteryWarnings=bLow && bCritical;
    Evidence.bBatteryCutoff=Drone.Battery.empty() && !Drone.Flight.armed && Drone.Flight.thrust==0. &&
        !Drone.TryArmFlight(Idle) && ReadHud(Idle,BatteryLabel()) && ReadHud(Idle,TEXT("SIM BAT EMPTY - F9 RECOVER/RECHARGE OR F10 RETURN"));
    if (!Evidence.bBatteryDrain || !Evidence.bBatteryWarnings || !Evidence.bBatteryCutoff) {
        return Fail(FString::Printf(TEXT("Battery steps failed. steps=%d poweredDrain=%d lowHUD=%d criticalHUD=%d cutoff=%d charge=%.6f armed=%d thrust=%.6f notice='%s'."),
            Evidence.BatteryCutoffSteps,Evidence.bBatteryDrain,bLow,bCritical,Evidence.bBatteryCutoff,PreviousPercent,Drone.Flight.armed,Drone.Flight.thrust,*Drone.Notice));
    }
    if (Drone.GetActorLocation().Equals(Launch,.01)) return Fail(TEXT("Battery cutoff did not leave a distinct pose for the actual F9 recovery."));
    RecoverKey.Execute(EKeys::F9);
    Evidence.Recharge=Drone.Battery.remainingPercent();
    Evidence.bBatteryRecovery=AtDisarmedLaunch() && Evidence.Recharge==100. && !Drone.Battery.empty() && ReadHud(Idle,BatteryLabel());
    if (!Evidence.bBatteryRecovery) return Fail(TEXT("Actual bound F9 did not return and recharge the empty simulated pack while disarmed."));

    Settings->bSimulateBattery=false;
    Settings->bCrashDamageEnabled=true;
    Settings->CrashToleranceJoules=30.f;
    Settings->ThrustToWeight=4.08f;
    Settings->Sanitize();
    Drone.ApplyFlightSettings(*Settings);
    const auto Drop=[&](double CutThrottleAtCm,double& MinimumNormalSpeed) {
        if (!Drone.TryArmFlight(Idle)) return false;
        bool bCoasting=false;
        for (int32 Step=0;Step<MaxImpactSteps;++Step) {
            const FVector Before=Drone.GetActorLocation();
            bCoasting|=Before.Z>=Launch.Z+CutThrottleAtCm;
            const double Incoming=Drone.Flight.velocity.z;
            Drone.SimulateFlight(bCoasting ? Idle : Powered,factoryfpv::StepSeconds);
            const FVector After=Drone.GetActorLocation();
            if (FVector::DistSquared2D(Launch,After)>FMath::Square(1.) || After.Z>=Upper.Z || After.ContainsNaN()) return false;
            if (!Drone.Flight.armed) {
                MinimumNormalSpeed=-Incoming;
                return bCoasting && MinimumNormalSpeed>3. && After.Equals(ExpectedLanding.Location,8.) && Drone.Flight.thrust==0.;
            }
        }
        return false;
    };
    double ModerateSpeed=0.,TerminalSpeed=0.;
    if (!Drone.Damage.enabled() || Drone.Battery.enabled() || !Drop(50.,ModerateSpeed))
        return Fail(TEXT("Actual takeoff, throttle cut and swept moderate impact did not return to the owned floor."));
    Evidence.ModerateIntegrity=Drone.Damage.integrityPercent();
    const double Nominal=Drone.Flight.parameters.mass*Drone.Flight.parameters.gravity*Drone.NominalThrustToWeight;
    if (Evidence.ModerateIntegrity<=0. || Evidence.ModerateIntegrity>=100. || Drone.Damage.destroyed() ||
        !(Drone.Flight.parameters.maxThrust>0. && Drone.Flight.parameters.maxThrust<Nominal) || !ReadHud(Idle,IntegrityLabel()))
        return Fail(TEXT("The moderate swept impact did not degrade the actual frame, available thrust and visible readout."));
    RecoverKey.Execute(EKeys::F9);
    if (!AtDisarmedLaunch() || Drone.Damage.integrityPercent()!=100. || !Drop(300.,TerminalSpeed))
        return Fail(TEXT("Moderate-impact F9 repair or the higher actual takeoff, throttle cut and swept terminal impact failed."));
    Evidence.TerminalIntegrity=Drone.Damage.integrityPercent();
    Evidence.bImpact=TerminalSpeed>ModerateSpeed && Evidence.TerminalIntegrity==0. && Drone.Damage.destroyed() &&
        Drone.Flight.parameters.maxThrust==0. && !Drone.TryArmFlight(Idle) && ReadHud(Idle,IntegrityLabel()) &&
        ReadHud(Idle,TEXT("FRAME LOST - F9 RECOVER/REPAIR OR F10 RETURN"));
    if (!Evidence.bImpact) return Fail(TEXT("Actual terminal swept impact did not destroy the frame, zero power, refuse arming and show frame loss."));
    RecoverKey.Execute(EKeys::F9);
    Evidence.Repair=Drone.Damage.integrityPercent();
    Evidence.bDamageRecovery=AtDisarmedLaunch() && Evidence.Repair==100. && !Drone.Damage.destroyed() &&
        FMath::IsNearlyEqual(Drone.Flight.parameters.maxThrust,Nominal) && ReadHud(Idle,IntegrityLabel());
    if (!Evidence.bDamageRecovery) return Fail(TEXT("Actual bound F9 did not repair the destroyed frame and return disarmed at the supported launch pose."));
    Restore();
    Evidence.bSettingsRestored=AtDisarmedLaunch() && Drone.Battery.enabled()==Committed->bSimulateBattery &&
        Drone.Battery.parameters.cells==Committed->BatteryCells && Drone.Battery.parameters.capacityMah==Committed->BatteryCapacityMah &&
        Drone.Battery.parameters.maxPowerWatts==Committed->BatteryMaxPowerWatts && Drone.Damage.enabled()==Committed->bCrashDamageEnabled &&
        Drone.Damage.parameters.destructionEnergyJoules==Committed->CrashToleranceJoules && Drone.Flight.parameters.mass==Committed->MassKg &&
        Drone.NominalThrustToWeight==Committed->ThrustToWeight && Drone.Body->GetUnscaledSphereRadius()==Committed->FrameSizeCm*.5f;
    return Evidence.bSettingsRestored || Fail(TEXT("The optional simulation probe did not restore the committed runtime tuning for ordinary Recovery."));
}
