#include "geometry/arc.h"

#include <algorithm>
#include <cmath>

namespace kirend {

std::optional<ArcInfo> ArcFromThreePoints( const Vec2& aStart, const Vec2& aMid, const Vec2& aEnd )
{
    const double ax = aStart.x, ay = aStart.y;
    const double bx = aMid.x, by = aMid.y;
    const double cx = aEnd.x, cy = aEnd.y;

    const double d = 2.0 * ( ax * ( by - cy ) + bx * ( cy - ay ) + cx * ( ay - by ) );

    if( std::abs( d ) < 1e-12 )
        return std::nullopt;

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;

    ArcInfo info;
    info.center = { ( a2 * ( by - cy ) + b2 * ( cy - ay ) + c2 * ( ay - by ) ) / d,
                    ( a2 * ( cx - bx ) + b2 * ( ax - cx ) + c2 * ( bx - ax ) ) / d };
    info.radius = ( aStart - info.center ).length();

    const double a0 = std::atan2( ay - info.center.y, ax - info.center.x );
    const double am = std::atan2( by - info.center.y, bx - info.center.x );
    const double a1 = std::atan2( cy - info.center.y, cx - info.center.x );

    auto normalize = []( double a )
    {
        while( a < 0.0 )
            a += 2.0 * PI;
        while( a >= 2.0 * PI )
            a -= 2.0 * PI;
        return a;
    };

    // Sweep counter-clockwise (increasing atan2) from a0 to a1; if mid is not on that path, the
    // arc goes the other way.
    const double ccwToEnd = normalize( a1 - a0 );
    const double ccwToMid = normalize( am - a0 );

    info.startAngle = a0;

    if( ccwToMid <= ccwToEnd )
        info.sweep = ccwToEnd;
    else
        info.sweep = ccwToEnd - 2.0 * PI;

    if( aStart == aEnd )
        info.sweep = ( ccwToMid > 0 ) ? 2.0 * PI : -2.0 * PI;

    return info;
}


int ArcSegmentCount( double aRadius, double aSweepRad, double aMaxError )
{
    if( aRadius <= aMaxError )
        return std::max( 1, static_cast<int>( std::ceil( std::abs( aSweepRad ) / ( PI / 2.0 ) ) ) );

    const double stepAngle = 2.0 * std::acos( 1.0 - aMaxError / aRadius );
    int          count = static_cast<int>( std::ceil( std::abs( aSweepRad ) / stepAngle ) );

    // Keep tiny arcs smooth and huge ones bounded.
    const int maxPerCircle = 360;
    const int maxCount = std::max( 1, static_cast<int>( std::ceil( maxPerCircle * std::abs( aSweepRad )
                                                                   / ( 2.0 * PI ) ) ) );
    return std::clamp( count, 1, std::max( 1, maxCount ) );
}


std::vector<Vec2> ApproximateArc( const Vec2& aStart, const Vec2& aMid, const Vec2& aEnd,
                                  double aMaxError )
{
    std::optional<ArcInfo> info = ArcFromThreePoints( aStart, aMid, aEnd );

    if( !info )
        return { aStart, aEnd };

    const int         count = ArcSegmentCount( info->radius, info->sweep, aMaxError );
    std::vector<Vec2> points;
    points.reserve( count + 1 );
    points.push_back( aStart );

    for( int i = 1; i < count; ++i )
    {
        const double a = info->startAngle + info->sweep * i / count;
        points.emplace_back( info->center.x + info->radius * std::cos( a ),
                             info->center.y + info->radius * std::sin( a ) );
    }

    points.push_back( aEnd );
    return points;
}


std::vector<Vec2> ApproximateCircle( const Vec2& aCenter, double aRadius, double aMaxError )
{
    int count = std::max( 8, ArcSegmentCount( aRadius, 2.0 * PI, aMaxError ) );

    std::vector<Vec2> points;
    points.reserve( count );

    for( int i = 0; i < count; ++i )
    {
        const double a = 2.0 * PI * i / count;
        points.emplace_back( aCenter.x + aRadius * std::cos( a ), aCenter.y + aRadius * std::sin( a ) );
    }

    return points;
}


std::vector<Vec2> ApproximateBezier( const Vec2& aP0, const Vec2& aP1, const Vec2& aP2,
                                     const Vec2& aP3, double aMaxError )
{
    // Uniform subdivision; the number of steps comes from the control polygon length.
    const double len = ( aP1 - aP0 ).length() + ( aP2 - aP1 ).length() + ( aP3 - aP2 ).length();
    const int    count = std::clamp( static_cast<int>( std::ceil( std::sqrt( len / aMaxError ) ) ), 2, 256 );

    std::vector<Vec2> points;
    points.reserve( count + 1 );

    for( int i = 0; i <= count; ++i )
    {
        const double t = static_cast<double>( i ) / count;
        const double u = 1.0 - t;
        points.push_back( aP0 * ( u * u * u ) + aP1 * ( 3 * u * u * t ) + aP2 * ( 3 * u * t * t )
                          + aP3 * ( t * t * t ) );
    }

    return points;
}

} // namespace kirend
