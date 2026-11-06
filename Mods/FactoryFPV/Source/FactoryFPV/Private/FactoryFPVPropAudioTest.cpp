#include "FactoryFPVPropAudioComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVPropAudioHandoffTest,
    "FactoryFPV.Audio.RenderHandoffAndMute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVPropAudioHandoffTest::RunTest(const FString&) {
    auto* Audio=NewObject<UFactoryFPVPropAudioComponent>();
    FSoundGeneratorInitParams Init;
    Init.SampleRate=48000;
    Init.NumChannels=1;
    auto Generator=Audio->CreateSoundGenerator(Init);
    auto* ReferenceAudio=NewObject<UFactoryFPVPropAudioComponent>();
    auto ReferenceGenerator=ReferenceAudio->CreateSoundGenerator(Init);
    factoryfpv::PropSoundParameters Parameters;
    Parameters.armed=true; Parameters.thrust=.9; Parameters.volume=.8;
    // A stalled consumer must resume with the latest throttle and live gain.
    for (int Index=0;Index<64;++Index) Audio->SetMotorState(Parameters);
    Parameters.thrust=.2; Parameters.volume=.35;
    Audio->SetMotorState(Parameters);
    ReferenceAudio->SetMotorState(Parameters);
    float Buffer[1024]{};
    float Expected[1024]{};
    const auto IsSilent=[](const float (&Samples)[1024]) {
        for (float Value:Samples) if (Value!=0) return false;
        return true;
    };
    Generator->OnGenerateAudio(Buffer,1024);
    ReferenceGenerator->OnGenerateAudio(Expected,1024);
    TestTrue(TEXT("First callback after a stalled consumer renders only the latest audible state"),
        FMemory::Memcmp(Buffer,Expected,sizeof(Buffer))==0);
    double Energy=0;
    for (int Block=0;Block<30;++Block) {
        Generator->OnGenerateAudio(Buffer,1024);
        for (float Value:Buffer) Energy+=Value*Value;
    }
    TestTrue(TEXT("Game-thread motor state reaches actual generator"),Energy>1);
    TestEqual(TEXT("Diagnostic sample counter observes rendering"),Audio->GetRenderedSampleCount(),static_cast<uint64>(31*1024));
    for (int Index=0;Index<64;++Index) Audio->SetMotorState(Parameters);
    Parameters.armed=false;
    Audio->SetMotorState(Parameters);
    Generator->OnGenerateAudio(Buffer,1024);
    TestFalse(TEXT("Manual disarm keeps the normal spin-down tail"),IsSilent(Buffer));
    for (int Block=0;Block<30;++Block) Generator->OnGenerateAudio(Buffer,1024);
    TestTrue(TEXT("Latest disarm still silences audio after a stalled consumer"),IsSilent(Buffer));

    Parameters.armed=true;
    Audio->SetMotorState(Parameters);
    Generator->OnGenerateAudio(Buffer,1024);
    TestFalse(TEXT("An armed voice makes motor noise before suspension"),IsSilent(Buffer));
    Audio->StopImmediately();
    Audio->SetMotorState(Parameters);
    auto RestartedGenerator=Audio->CreateSoundGenerator(Init);
    RestartedGenerator->OnGenerateAudio(Buffer,1024);
    TestFalse(TEXT("Re-arming starts a fresh voice with the current state"),IsSilent(Buffer));
    Generator->OnGenerateAudio(Buffer,1024);
    TestTrue(TEXT("A retired generator stays silent if it finishes work after restart"),IsSilent(Buffer));

    Parameters.volume=0;
    Audio->SetMotorState(Parameters);
    RestartedGenerator->OnGenerateAudio(Buffer,1024);
    TestTrue(TEXT("Setting zero volume hard mutes an audible generator"),IsSilent(Buffer));
    return true;
}
#endif
