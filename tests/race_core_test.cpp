#include "RaceCore.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace factoryfpv;

int main() {
    int count=0;
    auto check=[&](bool Value,const char* Name) { if(!Value) throw std::runtime_error(Name);++count; };
    auto close=[](double A,double B) { return std::abs(A-B)<1.e-8; };
    auto gate=[](const char* Id,int Order,double X) {
        RaceGate Gate;Gate.id=Id;Gate.order=Order;Gate.center={X,0,0};Gate.powered=true;return Gate;
    };
    std::vector<RaceGate> course{gate("persistent-guid-one",1,0),gate("persistent-guid-two",2,10)};
    RaceCore race;
    check(!race.state().selected && !race.state().running && !race.state().inLap,"racing defaults inactive");
    check(!race.start(),"cannot start without selecting a course");
    check(race.update(course,{-5,0,0},{15,0,0},20,.2).accepted==0,"unselected core cannot score");
    check(!race.select({}),"empty course rejected");
    check(!race.select({course[0]}),"single gate cannot define an ordered lap");
    auto malformed=course;malformed[1].id=malformed[0].id;
    check(!race.select(malformed),"duplicate persistent identities rejected");
    malformed=course;malformed[1].order=1;
    check(!race.select(malformed),"duplicate order rejected");
    malformed=course;malformed[1].order=3;
    check(!race.select(malformed),"gapped order rejected");
    malformed=course;malformed[0].id.clear();
    check(!race.select(malformed),"empty identity rejected");
    malformed=course;malformed[0].normal={2,0,0};
    check(!race.select(malformed),"nonunit plane normal rejected");
    malformed=course;malformed[0].right=malformed[0].normal;
    check(!race.select(malformed),"nonorthogonal aperture axes rejected");
    malformed=course;malformed[0].up={0,0,-1};
    check(!race.select(malformed),"left handed basis rejected");
    malformed=course;malformed[0].halfWidth=0;
    check(!race.select(malformed),"empty aperture rejected");
    malformed=course;malformed[0].center.x=std::numeric_limits<double>::quiet_NaN();
    check(!race.select(malformed),"nonfinite geometry rejected");
    malformed=course;malformed[0].powered=false;
    check(race.select(malformed) && !race.state().ready,"unpowered course can be selected but is not ready");
    check(!race.start(),"unpowered selected course cannot start");
    race.update(course,{0,0,0},{0,0,0},1,.2);
    check(race.state().ready && race.start(),"restored power permits explicit start");
    race.stop();
    check(race.select(course) && race.state().ready,"valid powered course selected");
    check(!race.state().running && !race.state().inLap,"selection alone cannot begin timing");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(!race.state().inLap && race.state().nextGate==1,"selected but stopped movement cannot score");

    auto fresh=[&](const std::vector<RaceGate>& Gates) {
        RaceCore Core;
        if(!Core.select(Gates) || !Core.start()) throw std::runtime_error("fresh valid fixture");
        return Core;
    };
    race=fresh(course);
    auto result=race.update(course,{-5,0,0},{5,0,0},10,.2);
    check(result.lapStarted && result.splitOrder==0 && race.currentLapSplits().empty() && race.lastLapSplits().empty(),
        "initial gate one starts the timer without emitting a checkpoint split");
    auto threeGates=course;threeGates.push_back(gate("persistent-guid-three",3,20));
    race=fresh(threeGates);
    result=race.update(threeGates,{-5,0,0},{25,0,0},3,.2);
    check(result.splitOrder==3 && close(result.splitElapsed,2) && race.currentLapSplits().size()==2 &&
        close(race.currentLapSplits()[0],1) && close(race.currentLapSplits()[1],2),
        "one sweep records each split in checkpoint order and reports the latest");

    race=fresh(course);
    result=race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(result.crossings==2 && result.accepted==2 && result.lapStarted,"one high-speed sweep crosses both ordered gates");
    check(race.state().inLap && race.state().nextGate==1 && close(race.state().elapsed,15),"start time interpolated within sweep");
    check(race.state().lapCount==0,"first start gate is not a completed lap");
    check(result.splitOrder==2 && close(result.splitElapsed,10) && race.lastLapSplits().empty(),
        "checkpoint split uses its swept timestamp instead of elapsed time at frame end");
    result=race.update(course,{15,0,0},{-5,0,0},20,.2);
    check(result.wrongWay==1 && result.crossings==0 && result.accepted==0 && !result.lapInvalidated &&
        race.state().inLap && race.state().nextGate==1 && close(race.state().elapsed,35),
        "reverse finish warns once without counting unrelated reverse gates or changing the active lap");
    check(result.splitOrder==0 && race.currentLapSplits().size()==1 && close(race.currentLapSplits()[0],10),
        "a reverse finish leaves the accepted checkpoint split alone");
    result=race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(result.lapCompleted && result.lapStarted && result.accepted==2,"finish crossing immediately starts the next lap and can pass next gate");
    check(race.state().lapCount==1 && close(race.state().lastLap,40) && close(race.state().bestLap,40),"complete lap uses interpolated start and finish timestamps");
    race.update(course,{15,0,0},{-5,0,0},10,.2);result=race.update(course,{-5,0,0},{15,0,0},10,.2);
    check(race.state().lapCount==2 && close(race.state().lastLap,27.5) && close(race.state().bestLap,27.5),"faster lap updates last and best records");
    check(result.lapCompleted && result.splitOrder==2 && close(result.splitElapsed,5) &&
        race.lastLapSplits().size()==1 && close(race.lastLapSplits()[0],10) &&
        race.currentLapSplits().size()==1 && close(race.currentLapSplits()[0],5),
        "a finish snapshots the old lap before recording the new lap split in the same sweep");
    race.update(course,{15,0,0},{-5,0,0},40,.2);race.update(course,{-5,0,0},{15,0,0},40,.2);
    check(race.state().lapCount==3 && race.state().lastLap>race.state().bestLap && close(race.state().bestLap,27.5),"slower lap leaves best record intact");
    check(race.lastLapSplits().size()==1 && close(race.lastLapSplits()[0],5) &&
        race.currentLapSplits().size()==1 && close(race.currentLapSplits()[0],20),
        "completed splits describe the last lap even when it was slower than the best");
    race.stop();
    check(!race.state().running && !race.state().inLap && race.state().lapCount==3,"stop cancels active lap but preserves completed records");
    check(race.currentLapSplits().empty() && race.lastLapSplits().size()==1 && close(race.lastLapSplits()[0],5),
        "stop discards the current splits and keeps the completed splits");
    race.start();
    check(!race.state().inLap && race.state().nextGate==1 && race.state().lapCount==3,"explicit restart waits for fresh gate1 and retains records");
    race.resetRun();
    check(!race.state().running && race.state().selected && race.state().lapCount==0 && race.state().bestLap==0,"reset run clears records without deselecting course");
    check(race.currentLapSplits().empty() && race.lastLapSplits().empty(),"reset clears both sets of splits");
    for (int Slices:{1,2,3,4,7,60,333}) {
        race=fresh(course);
        auto Leg=[&](Vec From,Vec To) {
            for (int Slice=0;Slice<Slices;++Slice) {
                const Vec A=From+(To-From)*(double(Slice)/Slices);
                const Vec B=From+(To-From)*(double(Slice+1)/Slices);
                race.update(course,A,B,20./Slices,.2);
            }
        };
        Leg({-5,0,0},{15,0,0});Leg({15,0,0},{-5,0,0});Leg({-5,0,0},{15,0,0});
        check(race.state().lapCount==1 && close(race.state().lastLap,40) && close(race.state().elapsed,15),
            "lap timing and progress are invariant to frame/substep subdivision");
    }

    race=fresh(course);
    check(race.update(course,{-1000,0,0},{1000,0,0},.01,.2).accepted==2,"high speed cannot tunnel through plane gates");
    race=fresh(course);
    check(race.update(course,{-5,-3,0},{5,3,0},1,.2).accepted==1,"aperture tested at crossing rather than outside endpoints");
    race=fresh(course);
    check(race.update(course,{-5,.8,0},{5,.8,0},1,.2).accepted==0,"sphere tangent to aperture edge is rejected");
    race=fresh(course);
    check(race.update(course,{-5,.799,0},{5,.799,0},1,.2).accepted==1,"sphere just inside aperture is accepted");
    race=fresh(course);
    check(race.update(course,{-5,0,.8},{5,0,.8},1,.2).accepted==0,"height accounts for drone collision radius");
    race=fresh(course);
    check(race.update(course,{-5,2,0},{5,2,0},1,0).accepted==0,"outside horizontal aperture rejected");
    race=fresh(course);
    check(race.update(course,{-5,0,2},{5,0,2},1,0).accepted==0,"outside vertical aperture rejected");
    race=fresh(course);
    check(race.update(course,{-5,0,0},{5,0,0},1,1).accepted==0,"drone larger than opening cannot start a lap");
    race=fresh(course);
    result=race.update(course,{5,0,0},{-5,0,0},1,.2);
    check(result.wrongWay==1 && result.accepted==0 && !race.state().inLap,"backward start gate warns without starting the lap");
    race=fresh(course);
    check(race.update(course,{15,0,0},{5,0,0},1,.2).wrongWay==0,"reverse pass through a different checkpoint does not warn");
    race=fresh(course);
    check(race.update(course,{5,.8,0},{-5,.8,0},1,.2).wrongWay==0,"reverse frame tangent does not warn");
    race=fresh(course);
    check(race.update(course,{5,0,2},{-5,0,2},1,.2).wrongWay==0,"reverse pass outside the aperture does not warn");
    race=fresh(course);
    check(race.update(course,{5,0,0},{0,0,0},1,.2).wrongWay==0,"arriving exactly on the plane from the front does not warn");
    check(race.update(course,{0,0,0},{-1.e-8,0,0},1,.2).wrongWay==1,"a split reverse crossing warns once as it leaves the plane");
    race.update(course,{-1.e-8,0,0},{1.e-8,0,0},1,.2);
    check(race.update(course,{1.e-8,0,0},{-1.e-8,0,0},1,.2).wrongWay==0,"reverse warning requires a fresh definite front approach");
    race=fresh(course);
    check(race.update(course,{5,0,0},{-5,0,0},1,.2,false).wrongWay==0,"a non-scoring reverse movement does not warn");
    race=fresh(course);
    race.update(course,{-5,0,0},{0,0,0},1,.2);
    check(!race.state().inLap,"touching the plane alone does not start timing");
    result=race.update(course,{0,0,0},{5,0,0},1,.2);
    check(result.accepted==1 && close(race.state().elapsed,1),"split sweep through exact plane endpoint scores once");
    check(race.update(course,{5,0,0},{6,0,0},1,.2).accepted==0,"remaining on forward side cannot repeat gate");
    race=fresh(course);
    race.update(course,{-5,0,0},{0,0,0},1,.2);
    race.update(course,{0,0,0},{0,2,0},5,.2);
    check(race.update(course,{0,2,0},{5,2,0},1,.2).accepted==0,"sliding along plane then exiting outside aperture cannot score");
    race=fresh(course);
    for(int Index=0;Index<20;++Index) {
        race.update(course,{-1.e-8,0,0},{1.e-8,0,0},.1,.2);
        race.update(course,{1.e-8,0,0},{-1.e-8,0,0},.1,.2);
    }
    check(!race.state().inLap && race.state().lapCount==0,"coplanar numerical jitter cannot manufacture an approach");
    race=fresh(course);
    check(race.update(course,{0,0,0},{0,0,0},1,.2).accepted==0,"coplanar stationary segment cannot score");

    auto rotated=course;
    for(auto& Gate:rotated) {
        Gate.center={100,200+Gate.center.x,300};Gate.normal={0,1,0};Gate.right={-1,0,0};
    }
    race=fresh(rotated);
    check(race.update(rotated,{100,195,300},{100,215,300},2,.2).accepted==2,"oriented translated gates use local aperture basis");
    auto rolled=course;
    for(auto& Gate:rolled) { Gate.right={0,0,1};Gate.up={0,-1,0};Gate.halfWidth=.5;Gate.halfHeight=2; }
    race=fresh(rolled);
    check(race.update(rolled,{-5,1.2,.1},{5,1.2,.1},1,.2).accepted==1,"rolled nonsquare aperture uses independent width and height axes");
    auto descending=course;
    descending[0].normal={0,0,-1};descending[0].up={1,0,0};
    race=fresh(descending);
    result=race.update(descending,{0,0,-5},{0,0,5},2,.2);
    check(result.wrongWay==1 && !result.lapStarted && race.state().nextGate==1,"ascending through a descent gate warns and keeps it expected");
    result=race.update(descending,{0,0,5},{0,0,-5},2,.2);
    check(result.wrongWay==0 && result.lapStarted && race.state().nextGate==2,"correct vertical retry advances without a reverse warning");
    auto reversed=course;std::reverse(reversed.begin(),reversed.end());
    race=fresh(reversed);
    check(race.update(reversed,{-5,0,0},{15,0,0},2,.2).accepted==2,"input enumeration order does not change course order");
    race=fresh(course);
    result=race.update(course,{5,0,0},{15,0,0},1,.2);
    check(result.wrongOrder==1 && !race.state().inLap && race.state().nextGate==1,"crossing gate2 first cannot start or advance a lap");
    std::vector<RaceGate> three{gate("one",1,0),gate("two",2,20),gate("three",3,10)};
    race=fresh(three);
    result=race.update(three,{-5,0,0},{25,0,0},3,.2);
    check(result.accepted==2 && result.wrongOrder==1 && race.state().nextGate==3,"crossings sorted by physical fraction, not required order");
    race.update(three,{25,0,0},{-5,0,0},3,.2);
    result=race.update(three,{-5,0,0},{5,0,0},1,.2);
    check(result.wrongOrder==1 && !result.lapCompleted && race.state().nextGate==3,"early start-gate crossing cannot finish or reset partial lap");
    race.update(three,{5,0,0},{15,0,0},1,.2);
    check(race.state().nextGate==1,"expected third gate eventually advances to finish");
    auto coincident=course;coincident[1].center=coincident[0].center;
    race=fresh(coincident);
    result=race.update(coincident,{-5,0,0},{5,0,0},1,.2);
    check(result.ambiguous==2 && result.accepted==0 && !race.state().inLap,"coincident gate planes cannot invent a scoring order");

    auto mixedDirection=course;
    mixedDirection[1].normal={-1,0,0};mixedDirection[1].right={0,-1,0};
    race=fresh(mixedDirection);
    result=race.update(mixedDirection,{-5,0,0},{15,0,0},20,.2);
    check(result.wrongWay==1 && result.accepted==1 && result.wrongOrder==0 && race.state().nextGate==2 &&
        race.state().inLap && close(race.state().elapsed,15),"a reverse pass can warn for a gate made expected earlier in the same sweep");
    result=race.update(mixedDirection,{15,0,0},{5,0,0},10,.2);
    check(result.wrongWay==0 && result.accepted==1 && race.state().nextGate==1,"correct pass after a reverse warning scores normally");
    mixedDirection[1].center.x=-2;
    race=fresh(mixedDirection);
    result=race.update(mixedDirection,{-5,0,0},{15,0,0},20,.2);
    check(result.wrongWay==0 && result.accepted==1 && race.state().nextGate==2,"reverse crossing before its predecessor is unrelated");
    mixedDirection[1].center.x=1.e-12;
    race=fresh(mixedDirection);
    result=race.update(mixedDirection,{-5,0,0},{15,0,0},20,.2);
    check(result.wrongWay==0 && result.accepted==1 && result.ambiguous==0 && race.state().nextGate==2,
        "simultaneous reverse pass cannot invent progression or make a valid forward pass ambiguous");

    race=fresh(course);
    race.update(course,{-5,0,0},{15,0,0},20,.2,false);
    check(!race.state().inLap && race.state().nextGate==1,"movement supplied during pause cannot start or advance a lap");
    check(race.update(course,{15,0,0},{16,0,0},1,.2).accepted==0,"resuming on front side cannot recover a paused crossing");

    race=fresh(course);race.update(course,{-5,0,0},{15,0,0},20,.2);
    const double beforePause=race.state().elapsed;
    race.update(course,{15,0,0},{-5,0,0},3600,.2,false);
    check(close(race.state().elapsed,beforePause) && race.state().nextGate==1,"pause adds neither time nor movement scoring");
    race.update(course,{15,0,0},{15,0,0},60,.2);
    check(close(race.state().elapsed,beforePause+60),"long active duration is counted without physics-step clamping");
    race.update(course,{15,0,0},{15,0,0},0,.2);
    check(race.state().inLap,"zero-duration stationary tick is harmless");
    result=race.update(course,{15,0,0},{-5,0,0},0,.2);
    check(result.lapInvalidated && !race.state().inLap,"zero-time movement cannot preserve a lap");

    race=fresh(course);race.update(course,{-5,0,0},{15,0,0},20,.2);
    auto live=course;live[1].powered=false;
    result=race.update(live,{15,0,0},{-5,0,0},20,.2);
    check(result.lapInvalidated && !race.state().ready && !race.state().inLap && race.state().nextGate==1,"power loss at any gate cancels partial lap");
    check(race.state().problem==RaceProblem::UnpoweredGate,"power loss has explicit status");
    race.update(course,{15,0,0},{16,0,0},1,.2);
    check(race.state().ready && !race.state().inLap && race.state().nextGate==1,"power restoration cannot resume cheated partial progress");
    race.update(course,{16,0,0},{-5,0,0},1,.2);
    check(race.update(course,{-5,0,0},{5,0,0},1,.2).lapStarted,"fresh powered start gate starts a new lap");
    race.invalidateLap();
    check(!race.state().inLap && race.state().nextGate==1 && race.state().problem==RaceProblem::Recovery,"F9 recovery invalidates current lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    live=course;live.pop_back();
    check(race.update(live,{15,0,0},{-5,0,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::MissingGate,"missing gate cannot score or retain lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    live=course;live[1].center.y=.1;
    check(race.update(live,{15,0,0},{-5,0,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::ChangedCourse,"geometry editing cannot retain partial lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    live=course;live[1].id="replacement-with-same-order";
    check(race.update(live,{15,0,0},{-5,0,0},1,.2).lapInvalidated,"replacement gate identity cannot retain partial lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    live=course;live[1].normal={};
    check(race.update(live,{15,0,0},{-5,0,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::InvalidCourse,"invalid live geometry cannot retain partial lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(race.update(course,{15,0,0},{-5,0,0},std::numeric_limits<double>::quiet_NaN(),.2).lapInvalidated,"nonfinite duration invalidates active lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(race.update(course,{15,0,0},{-5,0,0},1,-.2).lapInvalidated,"negative collision radius invalidates active lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(race.update(course,{15,0,0},{std::numeric_limits<double>::infinity(),0,0},1,.2).lapInvalidated,"nonfinite movement invalidates active lap");
    race.update(course,{-5,0,0},{15,0,0},20,.2);
    check(race.update(course,{-1.e308,0,0},{1.e308,0,0},1,.2).lapInvalidated,"overflowing finite coordinates cannot score");

    race=fresh(course);race.update(course,{-5,0,0},{15,0,0},20,.2);
    live=course;live[0].powered=false;
    check(race.update(live,{15,0,0},{15,0,0},60,.2,false).lapInvalidated && !race.state().ready,
        "power loss observed while paused still invalidates a partial lap");
    race=fresh(course);
    check(race.update(live,{-5,0,0},{15,0,0},1,.2).accepted==0 && !race.state().inLap,
        "live power failure blocks scoring even when start used an earlier powered snapshot");

    race=fresh(course);
    check(!race.refreshLive(course) && race.state().ready,"one stable frame sample prepares multiple sweeps");
    check(race.advanceSegment({-5,0,0},{5,0,0},10,.2).lapStarted && close(race.state().elapsed,5),
        "first prepared sweep starts the lap at the interpolated time");
    check(race.advanceSegment({5,0,0},{15,0,0},60,.2,false).accepted==0 &&
        race.state().nextGate==2 && close(race.state().elapsed,5),"paused sweep neither scores nor consumes time");
    check(race.advanceSegment({5,0,0},{15,0,0},10,.2).accepted==1 &&
        race.state().nextGate==1 && close(race.state().elapsed,15),"second prepared sweep advances without resampling");
    live=course;live[1].powered=false;
    check(race.refreshLive(live) && !race.state().ready && !race.state().inLap && race.state().problem==RaceProblem::UnpoweredGate,
        "once-per-frame power loss cancels the running lap before sweeps");
    check(!race.refreshLive(live),"repeated bad sample does not invalidate a lap twice");
    check(race.advanceSegment({-5,0,0},{15,0,0},20,.2).accepted==0 && race.state().problem==RaceProblem::UnpoweredGate,
        "unready frame blocks scoring and retains the live fault");
    std::reverse(live.begin(),live.end());
    check(!race.refreshLive(live) && race.state().problem==RaceProblem::UnpoweredGate,
        "unordered live snapshot reports the same power fault as an ordered snapshot");
    live[0].powered=true;
    check(!race.refreshLive(live) && race.state().ready && race.state().problem==RaceProblem::None && race.state().nextGate==1,
        "unordered snapshot with restored power clears the fault without restoring progress");
    check(race.advanceSegment({-5,0,0},{5,0,0},10,.2).lapStarted,"recovery requires a new gate-one crossing");
    live=course;live[1].center.y=.5e-6;
    check(!race.refreshLive(live) && race.state().ready && race.state().inLap,
        "changed snapshot within the existing geometry tolerance keeps the lap");
    live[1].center.y=2.e-6;live[0].powered=false;
    check(race.refreshLive(live) && race.state().problem==RaceProblem::ChangedCourse,
        "geometry beyond the tolerance wins over a simultaneous power fault");
    live[1].id=live[0].id;
    check(!race.refreshLive(live) && race.state().problem==RaceProblem::InvalidCourse,
        "duplicate live identity wins over geometry and power faults");

    race=fresh(course);race.update(course,{-5,0,0},{15,0,0},20,.2);
    race.update(course,{15,0,0},{-5,0,0},20,.2);race.update(course,{-5,0,0},{15,0,0},20,.2);
    race.invalidateLap();
    check(race.state().lapCount==1 && close(race.state().lastLap,40) && close(race.state().bestLap,40),"invalidating current lap preserves completed records");
    check(race.currentLapSplits().empty() && race.lastLapSplits().size()==1 && close(race.lastLapSplits()[0],10),
        "recovery clears the interrupted lap split and preserves the completed lap split");
    check(race.select(course) && race.state().lapCount==0 && race.state().bestLap==0,"new course selection starts a clean run");
    check(race.currentLapSplits().empty() && race.lastLapSplits().empty(),"selection clears the prior lap splits");

    // Open slalom poles share the ordered forward plane/timing rules. Their
    // scoring span starts outside the solid post on one explicitly chosen side;
    // it is not a centered rectangular opening or an infinite half-plane.
    check(course[0].kind==RaceGateKind::Gate && close(course[0].halfWidth,1) && close(course[0].halfHeight,1),
        "legacy gate construction preserves default rectangular geometry and kind");
    auto poles=course;
    for (auto& Pole:poles) { Pole.kind=RaceGateKind::PoleRight;Pole.poleRadius=.15;Pole.halfWidth=8;Pole.halfHeight=1.8; }
    malformed=poles;malformed[0].kind=static_cast<RaceGateKind>(255);
    check(!race.select(malformed),"unknown checkpoint kind rejected");
    malformed=poles;malformed[0].poleRadius=0;
    check(!race.select(malformed),"slalom pole requires a real solid post radius");
    malformed=poles;malformed[0].poleRadius=-.15;
    check(!race.select(malformed),"negative solid pole radius rejected");
    malformed=poles;malformed[0].poleRadius=std::numeric_limits<double>::quiet_NaN();
    check(!race.select(malformed),"nonfinite solid pole radius rejected");
    malformed=poles;malformed[0].poleRadius=malformed[0].halfWidth;
    check(!race.select(malformed),"pole without any open-side span rejected");

    auto polePass=[&](Vec From,Vec To,double Radius=.2) {
        race=fresh(poles);return race.update(poles,From,To,1,Radius);
    };
    check(polePass({-5,1,0},{5,1,0}).accepted==1,"forward pass right of right-open pole starts lap");
    check(polePass({-5,-1,0},{5,-1,0}).accepted==0,"wrong side of right-open pole does not count");
    check(polePass({-5,0,0},{5,0,0}).accepted==0,"direct solid-post crossing never counts");
    check(polePass({-5,.35,0},{5,.35,0}).accepted==0,"drone tangent to solid pole is rejected");
    check(polePass({-5,.351,0},{5,.351,0}).accepted==1,"drone strictly outside solid pole clearance is accepted");
    check(polePass({-5,7.8,0},{5,7.8,0}).accepted==0,"drone tangent to bounded outer open-side reach is rejected");
    check(polePass({-5,7.799,0},{5,7.799,0}).accepted==1,"drone just inside bounded outer reach is accepted");
    check(polePass({-5,9,0},{5,9,0}).accepted==0,"arbitrarily far pass beside pole cannot count");
    check(polePass({-5,1,1.6},{5,1,1.6}).accepted==0,"upper slalom scoring height includes drone radius");
    check(polePass({-5,1,-1.6},{5,1,-1.6}).accepted==0,"lower slalom scoring height includes drone radius");
    check(polePass({-5,1,1.599},{5,1,1.599}).accepted==1,"sphere wholly inside clear slalom height is accepted");
    check(polePass({-5,1,3},{5,1,3}).accepted==0,"flying over the slalom scoring height does not count");
    result=polePass({5,1,0},{-5,1,0});
    check(result.wrongWay==1 && result.accepted==0,"backward pass on correct open side warns without scoring");
    check(polePass({5,-1,0},{-5,-1,0}).wrongWay==0,"reverse pass on closed side of pole does not warn");
    check(polePass({5,.35,0},{-5,.35,0}).wrongWay==0,"reverse pass tangent to pole does not warn");
    check(polePass({5,1,1.6},{-5,1,1.6}).wrongWay==0,"reverse pass outside pole height clearance does not warn");
    check(polePass({-5,.15,0},{5,.15,0},0).accepted==0,"zero-radius path touching the solid post is rejected");
    check(polePass({-5,.151,0},{5,.151,0},0).accepted==1,"zero-radius path strictly outside the post can count");
    check(polePass({-5,-5,0},{5,7,0}).accepted==1,"slalom clearance is evaluated at the physical plane crossing");
    check(polePass({-1000,1,0},{1000,1,0}).accepted==2,"high-speed sweep cannot tunnel through ordered slalom planes");

    auto leftPoles=poles;for (auto& Pole:leftPoles) Pole.kind=RaceGateKind::PoleLeft;
    race=fresh(leftPoles);
    check(race.update(leftPoles,{-5,-1,0},{5,-1,0},1,.2).accepted==1,"left-open pole accepts negative local-right side");
    race=fresh(leftPoles);
    check(race.update(leftPoles,{-5,1,0},{5,1,0},1,.2).accepted==0,"left-open pole rejects positive local-right side");
    auto orientedPoles=poles;
    for (auto& Pole:orientedPoles) { Pole.center={100,200+Pole.center.x,300};Pole.normal={0,1,0};Pole.right={-1,0,0}; }
    race=fresh(orientedPoles);
    check(race.update(orientedPoles,{99,195,300},{99,205,300},1,.2).accepted==1,"slalom selected side follows translated rotated local basis");
    race=fresh(orientedPoles);
    check(race.update(orientedPoles,{101,195,300},{101,205,300},1,.2).accepted==0,"world-axis assumptions cannot swap a rotated slalom side");
    race=fresh(poles);race.update(poles,{-5,1,0},{0,1,0},1,.2);
    check(!race.state().inLap,"touching a pole plane alone does not score");
    check(race.update(poles,{0,1,0},{5,1,0},1,.2).accepted==1,"split behind-plane-front slalom path scores once");
    check(race.update(poles,{5,1,0},{6,1,0},1,.2).accepted==0,"remaining past pole cannot score repeatedly");
    race=fresh(poles);
    for (int Index=0;Index<20;++Index) {
        race.update(poles,{-1.e-8,1,0},{1.e-8,1,0},.1,.2);
        race.update(poles,{1.e-8,1,0},{-1.e-8,1,0},.1,.2);
    }
    check(!race.state().inLap,"slalom coplanar jitter cannot manufacture an approach");
    race=fresh(poles);result=race.update(poles,{5,1,0},{15,1,0},1,.2);
    check(result.wrongOrder==1 && result.accepted==0 && !race.state().inLap,"pole checkpoint before start cannot advance a course");

    std::vector<RaceGate> mixed{gate("frame-start",1,0),gate("right-pole",2,10),gate("left-pole",3,20)};
    mixed[0].halfWidth=3.8;mixed[0].halfHeight=1.8;
    for (int Index=1;Index<3;++Index) { mixed[Index].kind=Index==1?RaceGateKind::PoleRight:RaceGateKind::PoleLeft;mixed[Index].halfWidth=8;mixed[Index].halfHeight=1.8; }
    for (int Slices:{1,2,3,7,120}) {
        race=fresh(mixed);
        auto Leg=[&](Vec From,Vec To,double Seconds) {
            for (int Slice=0;Slice<Slices;++Slice)
                race.update(mixed,From+(To-From)*(double(Slice)/Slices),From+(To-From)*(double(Slice+1)/Slices),Seconds/Slices,.2);
        };
        Leg({-5,1,0},{15,1,0},2);
        check(race.state().nextGate==3 && race.state().inLap,"frame then right-open pole retain normal ordered progress");
        Leg({15,1,0},{15,-1,0},1);Leg({15,-1,0},{25,-1,0},1);
        check(race.state().nextGate==1,"opposite-side pole advances only after correct slalom pass");
        Leg({25,-1,0},{-5,-1,0},3);Leg({-5,-1,0},{5,-1,0},1);
        check(race.state().lapCount==1 && close(race.state().lastLap,7) && close(race.state().bestLap,7) && close(race.state().elapsed,.5),
            "mixed frame/slalom lap order and interpolated timing are subdivision invariant");
        check(race.currentLapSplits().empty() && race.lastLapSplits().size()==2 &&
            close(race.lastLapSplits()[0],1) && close(race.lastLapSplits()[1],3),
            "finished cumulative splits are subdivision invariant across different pole sides");
    }
    race.invalidateLap();
    check(race.state().lapCount==1 && close(race.state().bestLap,7) && !race.state().inLap,"recovery cancels mixed partial lap but keeps completed record");
    check(race.update(mixed,{5,1,0},{15,1,0},1,.2).accepted==0 && race.state().nextGate==1,"pole after recovery cannot resume old checkpoint progress");
    race=fresh(poles);
    check(race.update(poles,{-5,1,0},{15,1,0},1,.2,false).accepted==0 && !race.state().inLap,
        "caller-marked teleport or non-scoring recovery segment cannot score a slalom pole");
    check(race.update(poles,{15,1,0},{16,1,0},1,.2).accepted==0,"resuming beyond a skipped teleport crossing cannot claim it");
    race=fresh(poles);race.update(poles,{-5,1,0},{15,1,0},1,.2);live=poles;live[1].kind=RaceGateKind::PoleLeft;
    check(race.update(live,{15,1,0},{16,1,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::ChangedCourse,
        "changing selected open side invalidates live course layout");
    race=fresh(poles);race.update(poles,{-5,1,0},{15,1,0},1,.2);live=poles;live[1].poleRadius=.3;
    check(race.update(live,{15,1,0},{16,1,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::ChangedCourse,
        "changing solid pole radius invalidates live course layout");
    race=fresh(poles);race.update(poles,{-5,1,0},{15,1,0},1,.2);live=poles;live[1].powered=false;
    check(race.update(live,{15,1,0},{16,1,0},1,.2).lapInvalidated && race.state().problem==RaceProblem::UnpoweredGate,
        "unpowered slalom checkpoint cancels partial lap like a framed gate");
    std::cout<<count<<" ordered race checks passed\n";
}
