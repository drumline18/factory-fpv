#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGate.h"
#include "Subsystem/SubsystemActorManager.h"

namespace {
bool ValidCourseId(int32 CourseId) {
    return CourseId>=1 && CourseId<=AFactoryFPVRaceGate::MaximumCourseId;
}

bool NormalizeCourseName(FString& Name) {
    Name.TrimStartAndEndInline();
    if (Name.Len()>AFactoryFPVRaceCourseRegistry::MaximumNameLength) return false;
    for (const TCHAR Character : Name) {
        if (FChar::IsControl(Character) || Character==0x2028 || Character==0x2029) return false;
    }
    return true;
}
}

AFactoryFPVRaceCourseRegistry::AFactoryFPVRaceCourseRegistry() {
    ReplicationPolicy=ESubsystemReplicationPolicy::SpawnOnServer;
}

AFactoryFPVRaceCourseRegistry* AFactoryFPVRaceCourseRegistry::Get(UWorld* World) {
    auto* Manager=World && World->IsGameWorld() && World->GetNetMode()==NM_Standalone && !World->bIsTearingDown ?
        World->GetSubsystem<USubsystemActorManager>() : nullptr;
    return Manager ? Manager->GetSubsystemActor<AFactoryFPVRaceCourseRegistry>() : nullptr;
}

bool AFactoryFPVRaceCourseRegistry::SetCourseName(int32 CourseId,FString Name,FString& Error) {
    Error.Reset();
    if (!IsInGameThread() || !HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode()!=NM_Standalone ||
        GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || !ValidCourseId(CourseId)) {
        Error=TEXT("Choose a course in a loaded single-player save.");
        return false;
    }
    if (!NormalizeCourseName(Name)) {
        Error=FString::Printf(TEXT("Use a single line of up to %d characters for the course name."),MaximumNameLength);
        return false;
    }
    if (Name.IsEmpty()) CourseNames.Remove(CourseId);
    else CourseNames.Add(CourseId,MoveTemp(Name));
    return true;
}

void AFactoryFPVRaceCourseRegistry::PostLoadGame_Implementation(int32,int32) {
    for (auto It=CourseNames.CreateIterator();It;++It) {
        if (!ValidCourseId(It.Key()) || !NormalizeCourseName(It.Value()) || It.Value().IsEmpty()) It.RemoveCurrent();
    }
}
