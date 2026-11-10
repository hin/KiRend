// Arc helpers.
#pragma once

#include "geometry/vec.h"

#include <optional>
#include <vector>

namespace kirend {

/// Maximum distance between a true arc and its polyline approximation, in mm.
constexpr double ARC_MAX_ERROR_MM = 0.005;

struct ArcInfo
{
    Vec2   center;
    double radius = 0.0;
    double startAngle = 0.0;  ///< radians, atan2 in the coordinate system of the points
    double sweep = 0.0;       ///< signed sweep in radians from start through mid to end
};

/// Circle through three points. Returns nullopt for (nearly) collinear points.
std::optional<ArcInfo> ArcFromThreePoints( const Vec2& aStart, const Vec2& aMid, const Vec2& aEnd );

/// Number of segments needed so the chord error stays below aMaxError.
int ArcSegmentCount( double aRadius, double aSweepRad, double aMaxError = ARC_MAX_ERROR_MM );

/// Polyline approximation of an arc through three points, including both end points.
std::vector<Vec2> ApproximateArc( const Vec2& aStart, const Vec2& aMid, const Vec2& aEnd,
                                  double aMaxError = ARC_MAX_ERROR_MM );

/// Polyline approximation of a full circle (closed: first point is not repeated).
std::vector<Vec2> ApproximateCircle( const Vec2& aCenter, double aRadius,
                                     double aMaxError = ARC_MAX_ERROR_MM );

/// Polyline approximation of a cubic Bezier curve, including both end points.
std::vector<Vec2> ApproximateBezier( const Vec2& aP0, const Vec2& aP1, const Vec2& aP2,
                                     const Vec2& aP3, double aMaxError = ARC_MAX_ERROR_MM );

} // namespace kirend
