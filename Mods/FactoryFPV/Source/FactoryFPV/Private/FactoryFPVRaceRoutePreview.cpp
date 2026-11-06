#include "FactoryFPVRaceRoutePreview.h"
#include "FactoryFPVRaceGate.h"

#include "Application/SlateApplicationBase.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace FactoryFPVRaceRoutePreviewInternal {
constexpr FLinearColor Background(.025f,.032f,.042f);
constexpr FLinearColor Grid(.085f,.11f,.14f);
constexpr FLinearColor RouteColor(.16f,.54f,.62f);
constexpr FLinearColor Powered(.20f,.86f,.89f);
constexpr FLinearColor Unpowered(1.f,.48f,.12f);
constexpr FLinearColor Muted(.62f,.70f,.76f);

double NiceDistance(double Distance,bool bRoundUp) {
    const double Magnitude=FMath::Pow(10.,FMath::FloorToDouble(FMath::LogX(10.,Distance)));
    const double Unit=Distance/Magnitude;
    if (bRoundUp) return Magnitude*(Unit<=1. ? 1. : Unit<=2. ? 2. : Unit<=5. ? 5. : 10.);
    return Magnitude*(Unit>=5. ? 5. : Unit>=2. ? 2. : 1.);
}

struct FMarker {
    FVector2f Point;
    TArray<int32> Orders;
    bool bPowered=true;
    bool bSelected=false;
    EFactoryFPVRaceGateDirection Direction=EFactoryFPVRaceGateDirection::Forward;

    bool IsStart() const { return Orders[0]==1; }
    int32 LabelPriority() const { return bSelected ? 2 : IsStart() ? 1 : 0; }
    FString Label() const {
        FString Result=FString::FromInt(Orders[0]);
        if (Orders.Num()>1) Result+=FString::Printf(TEXT("/%d"),Orders[1]);
        if (Orders.Num()>2) Result+=FString::Printf(TEXT("+%d"),Orders.Num()-2);
        return Result;
    }
};
}

void SFactoryFPVRaceRoutePreview::SetRoute(TArray<FCheckpoint> InCheckpoints) {
    if (Checkpoints==InCheckpoints) return;
    Checkpoints=MoveTemp(InCheckpoints);
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SFactoryFPVRaceRoutePreview::SetElevation(bool bEnabled) {
    if (bElevation==bEnabled) return;
    bElevation=bEnabled;
    Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SFactoryFPVRaceRoutePreview::OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool) const {
    using namespace FactoryFPVRaceRoutePreviewInternal;
    const FVector2f Size(Geometry.GetLocalSize());
    const auto Tint=Style.GetColorAndOpacityTint();
    const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const auto Paint=Geometry.ToPaintGeometry();
    FSlateDrawElement::MakeBox(Elements,Layer,Paint,Brush,ESlateDrawEffect::None,Background*Tint);
    if (Checkpoints.Num()<2 || Size.X<150.f || Size.Y<85.f) return Layer;

    const auto& Font=FCoreStyle::GetDefaultFontStyle(TEXT("Mono"),9);
    constexpr float GlyphWidth=6.2f;
    constexpr float LabelHeight=13.f;
    const auto Line=[&](int32 Offset,const TArray<FVector2f>& Points,const FLinearColor& Color,float Width=1.f) {
        FSlateDrawElement::MakeLines(Elements,Layer+Offset,Paint,Points,ESlateDrawEffect::None,Color*Tint,true,Width);
    };
    const auto Text=[&](int32 Offset,const FString& Value,FVector2f At,const FLinearColor& Color) {
        const FVector2f Bounds(GlyphWidth*Value.Len(),LabelHeight);
        FSlateDrawElement::MakeText(Elements,Layer+Offset,Geometry.ToPaintGeometry(Bounds,FSlateLayoutTransform(At)),
            Value,Font,ESlateDrawEffect::None,Color*Tint);
    };

    const FVector2f TopLeft(bElevation ? 62.f : 12.f,15.f);
    const FVector2f BottomRight=Size-FVector2f(13.f,26.f);
    const FVector2f PlotSize=BottomRight-TopLeft;
    const FVector Origin=Checkpoints[0].Position;
    TArray<FVector2f> Route;
    Route.Reserve(Checkpoints.Num()+1);

    if (bElevation) {
        double Minimum=0.,Maximum=0.;
        for (const auto& Point:Checkpoints) {
            const double Height=(Point.Position.Z-Origin.Z)/100.;
            Minimum=FMath::Min(Minimum,Height);
            Maximum=FMath::Max(Maximum,Height);
        }
        const double Step=NiceDistance(FMath::Max(1.,Maximum-Minimum)/3.,true);
        const int32 BottomTick=FMath::FloorToInt(Minimum/Step);
        const int32 TopTick=FMath::Max(BottomTick+1,FMath::CeilToInt(Maximum/Step));
        const auto ProjectY=[&](double Height) {
            return BottomRight.Y-PlotSize.Y*static_cast<float>((Height/Step-BottomTick)/(TopTick-BottomTick));
        };
        for (int32 Index=BottomTick;Index<=TopTick;++Index) {
            const float Y=ProjectY(Index*Step);
            Line(1,{{TopLeft.X,Y},{BottomRight.X,Y}},Index==0 ? RouteColor*.6f : Grid);
            const FString Tick=Index==0 ? TEXT("0 m") : FString::Printf(TEXT("%+.3g m"),Index*Step);
            Text(1,Tick,{FMath::Max(2.f,TopLeft.X-17.f-GlyphWidth*Tick.Len()),Y-LabelHeight*.5f},Muted);
        }
        for (const auto& Point:Checkpoints) {
            const float X=TopLeft.X+PlotSize.X*static_cast<float>(Point.Order-1)/(Checkpoints.Num()-1);
            Route.Emplace(X,ProjectY((Point.Position.Z-Origin.Z)/100.));
        }
        const auto* Renderer=FSlateApplicationBase::Get().GetRenderer();
        const auto AxisWidth=[&](const FString& Label) -> float {
            return Renderer ? Renderer->GetFontMeasureService()->Measure(Label,Font).X : Label.Len()*8.f;
        };
        const FString DirectionAxis=TEXT("Order  /  Up for Ascend  /  Down for Descend");
        const FString Axis=PlotSize.X>AxisWidth(DirectionAxis)+4.f && Checkpoints.ContainsByPredicate([](const auto& Point) {
            return Point.Direction!=EFactoryFPVRaceGateDirection::Forward;
        }) ? DirectionAxis : TEXT("Checkpoint order");
        Text(1,Axis,{(TopLeft.X+BottomRight.X-AxisWidth(Axis))*.5f,Size.Y-17.f},Muted);
    } else {
        FVector2D Minimum=FVector2D::ZeroVector,Maximum=FVector2D::ZeroVector;
        for (const auto& Point:Checkpoints) {
            const FVector2D Position((Point.Position-Origin)/100.);
            Minimum.X=FMath::Min(Minimum.X,Position.X); Minimum.Y=FMath::Min(Minimum.Y,Position.Y);
            Maximum.X=FMath::Max(Maximum.X,Position.X); Maximum.Y=FMath::Max(Maximum.Y,Position.Y);
        }
        const FVector2D Range=Maximum-Minimum;
        const double Scale=FMath::Min(PlotSize.X/FMath::Max(1.,Range.X),PlotSize.Y/FMath::Max(1.,Range.Y));
        const FVector2D Center=(Minimum+Maximum)*.5;
        const FVector2f PlotCenter=(TopLeft+BottomRight)*.5f;
        for (const auto& Point:Checkpoints) {
            const FVector2D Relative=FVector2D((Point.Position-Origin)/100.)-Center;
            Route.Add(PlotCenter+FVector2f(Relative.X*Scale,-Relative.Y*Scale));
        }
        const double ScaleMeters=NiceDistance(70./Scale,false);
        const float ScalePixels=static_cast<float>(ScaleMeters*Scale);
        const FVector2f Left(14.f,Size.Y-11.f),Right(Left.X+ScalePixels,Left.Y);
        Line(1,{Left-FVector2f(0,3),Left,Right,Right-FVector2f(0,3)},Muted);
        Text(1,FString::Printf(TEXT("%.3g m"),ScaleMeters),Right+FVector2f(7.f,-LabelHeight*.65f),Muted);
    }

    TArray<FMarker> Markers;
    Markers.Reserve(Checkpoints.Num());
    for (int32 Index=0;Index<Checkpoints.Num();++Index) {
        const auto& Checkpoint=Checkpoints[Index];
        const FVector2f Point=Route[Index];
        FMarker* Marker=Markers.FindByPredicate([Point](const FMarker& Item) { return Item.Point.Equals(Point,.35f); });
        if (!Marker) {
            Marker=&Markers.AddDefaulted_GetRef();
            Marker->Point=Point;
        }
        Marker->Orders.Add(Checkpoint.Order);
        Marker->bPowered&=Checkpoint.bPowered;
        Marker->bSelected|=Checkpoint.bSelected;
        if (bElevation) Marker->Direction=Checkpoint.Direction;
    }
    if (!bElevation && Route.Num()>2) {
        const FVector2f Start=Route[0];
        Route.Add(Start);
    }
    if (Markers.Num()>1) Line(2,Route,RouteColor,1.5f);

    Markers.StableSort([](const FMarker& A,const FMarker& B) { return A.LabelPriority()>B.LabelPriority(); });
    TArray<FSlateRect> LabelBounds;
    for (const auto& Marker:Markers) {
        const FVector2f Point=Marker.Point;
        const auto Color=Marker.bPowered ? Powered : Unpowered;
        const bool bDirectional=Marker.Direction!=EFactoryFPVRaceGateDirection::Forward;
        const float Radius=Marker.IsStart() ? (bDirectional ? 7.f : 5.f) : (bDirectional ? 6.f : 3.f);
        const auto Diamond=[&](float R) -> TArray<FVector2f> {
            return {Point+FVector2f(0,-R),Point+FVector2f(R,0),Point+FVector2f(0,R),Point+FVector2f(-R,0),Point+FVector2f(0,-R)};
        };
        if (Marker.bSelected) Line(5,Diamond(Radius+4.f),FLinearColor::White,1.5f);
        if (Marker.IsStart()) Line(4,Diamond(Radius),Color,2.f);
        if (bDirectional) {
            const float Y=Marker.Direction==EFactoryFPVRaceGateDirection::Ascend ? -4.5f : 4.5f;
            const float Arm=Radius*(Marker.IsStart() ? .6f : .85f);
            Line(4,{Point+FVector2f(-Arm,-Y*.5f),Point+FVector2f(0.f,Y),
                Point+FVector2f(Arm,-Y*.5f)},Color,2.25f);
        } else if (!Marker.IsStart()) {
            const FVector2f Extent(Radius,Radius);
            FSlateDrawElement::MakeBox(Elements,Layer+3,Geometry.ToPaintGeometry(Extent*2.f,FSlateLayoutTransform(Point-Extent)),
                Brush,ESlateDrawEffect::None,Color*Tint);
        }

        const FString Label=Marker.Label();
        const float Width=GlyphWidth*Label.Len();
        const FVector2f Candidates[]={{Point.X+8.f,Point.Y-16.f},{Point.X-Width-8.f,Point.Y-16.f},
            {Point.X+8.f,Point.Y+4.f},{Point.X-Width-8.f,Point.Y+4.f}};
        for (const FVector2f& At:Candidates) {
            const FSlateRect Bounds(At-FVector2f(2.f,1.f),At+FVector2f(Width+2.f,LabelHeight));
            if (Bounds.Left<1.f || Bounds.Right>Size.X-1.f || Bounds.Top<1.f || Bounds.Bottom>Size.Y-20.f ||
                (bElevation && Bounds.Left<TopLeft.X-3.f)) continue;
            if (LabelBounds.ContainsByPredicate([&](const FSlateRect& Other) { return FSlateRect::DoRectanglesIntersect(Bounds,Other); }) ||
                Markers.ContainsByPredicate([&](const FMarker& Other) { return Bounds.ContainsPoint(Other.Point); })) continue;
            Text(6,Label,At,Marker.bSelected ? FLinearColor::White : Color);
            LabelBounds.Add(Bounds);
            break;
        }
    }
    return Layer+6;
}
