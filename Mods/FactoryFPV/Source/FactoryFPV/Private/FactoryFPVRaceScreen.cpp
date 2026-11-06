#include "FactoryFPVRaceScreen.h"

FVector2D FactoryFPVRaceScreen::OffscreenDirection(const FVector& Target,const FVector2D& ProjectionScale,
    double AspectRatio,double LensStrength) {
    if (Target.ContainsNaN() || Target.IsNearlyZero() || ProjectionScale.ContainsNaN() ||
        ProjectionScale.X<=0. || ProjectionScale.Y<=0. || !FMath::IsFinite(AspectRatio) || AspectRatio<=0.) {
        return FVector2D::ZeroVector;
    }

    const FVector2D ProjectedRay(Target.Y*ProjectionScale.X,-Target.Z*ProjectionScale.Y);
    const double EdgeComponent=FMath::Max(FMath::Abs(ProjectedRay.X),FMath::Abs(ProjectedRay.Y));
    // A target directly behind has no projected side. Use the lower edge,
    // matching the existing compass, and suppress jitter around the rear axis.
    if (Target.X<0. && EdgeComponent<=-.01*Target.X) return FVector2D(0.,1.);
    if (EdgeComponent<=UE_SMALL_NUMBER || !FMath::IsFinite(EdgeComponent)) return FVector2D::ZeroVector;

    const FVector2D Edge=ProjectedRay/EdgeComponent;
    const double Strength=FMath::IsFinite(LensStrength) ? FMath::Clamp(LensStrength,0.,1.) : 0.;
    // M_FPVLens samples inward along the same ray. Compute the original
    // image coordinate the lens samples at this edge of the displayed frame.
    const double AspectSquared=FMath::Square(AspectRatio);
    const double RadiusSquared=(FMath::Square(Edge.X)*AspectSquared+FMath::Square(Edge.Y))/(AspectSquared+1.);
    const double SourceEdgeScale=(1.+Strength*RadiusSquared)/(1.+Strength);
    if (Target.X>0. && EdgeComponent<=Target.X*SourceEdgeScale) return FVector2D::ZeroVector;
    return Edge.GetSafeNormal();
}
