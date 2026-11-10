#include "geometry/outline.h"

#include "geometry/arc.h"

#include <algorithm>
#include <cmath>

namespace kirend {

using pcb::Shape;

void Box2::merge( const Vec2& p )
{
    min.x = std::min( min.x, p.x );
    min.y = std::min( min.y, p.y );
    max.x = std::max( max.x, p.x );
    max.y = std::max( max.y, p.y );
}


void Box2::merge( const Box2& b )
{
    if( b.valid() )
    {
        merge( b.min );
        merge( b.max );
    }
}


void Box2::inflate( double d )
{
    min.x -= d;
    min.y -= d;
    max.x += d;
    max.y += d;
}


namespace {

Box2 arcBoundingBox( const Vec2& aStart, const Vec2& aMid, const Vec2& aEnd )
{
    Box2 box;
    box.merge( aStart );
    box.merge( aEnd );

    std::optional<ArcInfo> info = ArcFromThreePoints( aStart, aMid, aEnd );

    if( !info )
    {
        box.merge( aMid );
        return box;
    }

    // Add the axis extreme points that lie within the sweep.
    for( int quadrant = 0; quadrant < 4; ++quadrant )
    {
        const double a = quadrant * PI / 2.0;
        double       rel = a - info->startAngle;

        if( info->sweep >= 0 )
        {
            while( rel < 0 )
                rel += 2 * PI;
            while( rel >= 2 * PI )
                rel -= 2 * PI;

            if( rel > info->sweep )
                continue;
        }
        else
        {
            while( rel > 0 )
                rel -= 2 * PI;
            while( rel <= -2 * PI )
                rel += 2 * PI;

            if( rel < info->sweep )
                continue;
        }

        box.merge( Vec2( info->center.x + info->radius * std::cos( a ),
                         info->center.y + info->radius * std::sin( a ) ) );
    }

    return box;
}

} // namespace


Box2 ShapeBoundingBox( const Shape& aShape )
{
    Box2 box;

    switch( aShape.kind )
    {
    case Shape::Kind::Segment:
    case Shape::Kind::Rect:
        box.merge( aShape.start );
        box.merge( aShape.end );
        break;

    case Shape::Kind::Circle:
    {
        const double r = ( aShape.end - aShape.start ).length();
        box.merge( aShape.start );
        box.inflate( r );
        break;
    }

    case Shape::Kind::Arc:
        box = arcBoundingBox( aShape.start, aShape.mid, aShape.end );
        break;

    case Shape::Kind::Poly:
        for( const Vec2& p : aShape.points )
            box.merge( p );
        break;

    case Shape::Kind::Bezier:
        for( const Vec2& p : ApproximateBezier( aShape.start, aShape.ctrl1, aShape.ctrl2, aShape.end ) )
            box.merge( p );
        break;
    }

    if( aShape.width > 0 )
        box.inflate( aShape.width / 2.0 );

    return box;
}


std::optional<Polygon> ClosedShapeToPolygon( const Shape& aShape )
{
    switch( aShape.kind )
    {
    case Shape::Kind::Circle:
        return ApproximateCircle( aShape.start, ( aShape.end - aShape.start ).length() );

    case Shape::Kind::Rect:
    {
        const Vec2 a = aShape.start;
        const Vec2 b = aShape.end;
        const double r = std::min( { aShape.cornerRadius, std::abs( b.x - a.x ) / 2,
                                     std::abs( b.y - a.y ) / 2 } );

        if( r <= 0 )
            return Polygon{ a, { b.x, a.y }, b, { a.x, b.y } };

        // Rounded rectangle: corners as quarter circles.
        const double x0 = std::min( a.x, b.x ), x1 = std::max( a.x, b.x );
        const double y0 = std::min( a.y, b.y ), y1 = std::max( a.y, b.y );
        Polygon      poly;

        auto corner = [&]( Vec2 c, double startDeg )
        {
            const double s = DegToRad( startDeg );
            std::vector<Vec2> arc = ApproximateArc( c + Vec2( std::cos( s ), std::sin( s ) ) * r,
                                                    c + Vec2( std::cos( s + PI / 4 ), std::sin( s + PI / 4 ) ) * r,
                                                    c + Vec2( std::cos( s + PI / 2 ), std::sin( s + PI / 2 ) ) * r );
            poly.insert( poly.end(), arc.begin(), arc.end() );
        };

        corner( { x1 - r, y0 + r }, 270 );
        corner( { x1 - r, y1 - r }, 0 );
        corner( { x0 + r, y1 - r }, 90 );
        corner( { x0 + r, y0 + r }, 180 );
        return poly;
    }

    case Shape::Kind::Poly:
        if( aShape.points.size() >= 3 )
            return aShape.points;

        return std::nullopt;

    default:
        return std::nullopt;
    }
}


std::vector<Vec2> OpenShapeToPolyline( const Shape& aShape )
{
    switch( aShape.kind )
    {
    case Shape::Kind::Segment: return { aShape.start, aShape.end };
    case Shape::Kind::Arc: return ApproximateArc( aShape.start, aShape.mid, aShape.end );
    case Shape::Kind::Bezier:
        return ApproximateBezier( aShape.start, aShape.ctrl1, aShape.ctrl2, aShape.end );
    default: return {};
    }
}


std::vector<const Shape*> EdgeCutShapes( const pcb::Board& aBoard )
{
    std::vector<const Shape*> shapes;

    for( const Shape& s : aBoard.drawings )
    {
        if( s.layer == "Edge.Cuts" )
            shapes.push_back( &s );
    }

    for( const pcb::Footprint& fp : aBoard.footprints )
    {
        for( const Shape& s : fp.graphics )
        {
            if( s.layer == "Edge.Cuts" )
                shapes.push_back( &s );
        }
    }

    return shapes;
}


double SignedArea( const Polygon& aPoly )
{
    double area = 0.0;

    for( size_t i = 0, n = aPoly.size(); i < n; ++i )
        area += Cross( aPoly[i], aPoly[( i + 1 ) % n] );

    return area / 2.0;
}


bool PointInPolygon( const Vec2& aPoint, const Polygon& aPoly )
{
    bool inside = false;

    for( size_t i = 0, j = aPoly.size() - 1; i < aPoly.size(); j = i++ )
    {
        const Vec2& a = aPoly[i];
        const Vec2& b = aPoly[j];

        if( ( a.y > aPoint.y ) != ( b.y > aPoint.y )
            && aPoint.x < ( b.x - a.x ) * ( aPoint.y - a.y ) / ( b.y - a.y ) + a.x )
        {
            inside = !inside;
        }
    }

    return inside;
}


BoardOutline BuildBoardOutline( const pcb::Board& aBoard )
{
    BoardOutline result;

    // KiCad's default outline chaining epsilon.
    constexpr double CHAIN_EPSILON = 0.01; // mm

    std::vector<Polygon>           closed;
    std::vector<std::vector<Vec2>> open;

    for( const Shape* s : EdgeCutShapes( aBoard ) )
    {
        if( std::optional<Polygon> poly = ClosedShapeToPolygon( *s ) )
            closed.push_back( std::move( *poly ) );
        else if( std::vector<Vec2> line = OpenShapeToPolyline( *s ); line.size() >= 2 )
            open.push_back( std::move( line ) );
    }

    // Chain open pieces into closed loops.
    std::vector<bool> used( open.size(), false );
    bool              chainError = false;

    for( size_t i = 0; i < open.size(); ++i )
    {
        if( used[i] )
            continue;

        used[i] = true;
        std::vector<Vec2> chain = open[i];

        while( ( chain.front() - chain.back() ).length() > CHAIN_EPSILON )
        {
            bool   found = false;
            size_t best = 0;
            bool   reverse = false;
            double bestDist = CHAIN_EPSILON;

            for( size_t j = 0; j < open.size(); ++j )
            {
                if( used[j] )
                    continue;

                double dFront = ( open[j].front() - chain.back() ).length();
                double dBack = ( open[j].back() - chain.back() ).length();

                if( dFront <= bestDist )
                {
                    bestDist = dFront;
                    best = j;
                    reverse = false;
                    found = true;
                }

                if( dBack <= bestDist )
                {
                    bestDist = dBack;
                    best = j;
                    reverse = true;
                    found = true;
                }
            }

            if( !found )
                break;

            used[best] = true;
            std::vector<Vec2> next = open[best];

            if( reverse )
                std::reverse( next.begin(), next.end() );

            chain.insert( chain.end(), next.begin() + 1, next.end() );
        }

        if( ( chain.front() - chain.back() ).length() <= CHAIN_EPSILON && chain.size() >= 4 )
        {
            chain.pop_back();
            closed.push_back( std::move( chain ) );
        }
        else
        {
            chainError = true;
        }
    }

    if( closed.empty() || chainError )
    {
        result.fromEdgeCuts = false;
        result.warning = closed.empty() && !chainError
                                 ? "board has no Edge.Cuts outline; using the board bounding box"
                                 : "board outline (Edge.Cuts) is not closed; using the board bounding box";
        return result;
    }

    // Nesting depth decides outline vs hole: even depth = outline, odd = hole.
    std::vector<int> depth( closed.size(), 0 );

    for( size_t i = 0; i < closed.size(); ++i )
    {
        for( size_t j = 0; j < closed.size(); ++j )
        {
            if( i != j && PointInPolygon( closed[i].front(), closed[j] ) )
                ++depth[i];
        }
    }

    for( size_t i = 0; i < closed.size(); ++i )
    {
        if( depth[i] % 2 == 0 )
        {
            OutlinePiece piece;
            piece.outer = closed[i];
            result.pieces.push_back( std::move( piece ) );
        }
    }

    for( size_t i = 0; i < closed.size(); ++i )
    {
        if( depth[i] % 2 == 1 )
        {
            // Attach to the innermost containing outline.
            int bestPiece = -1;
            double bestArea = 1e300;

            for( size_t p = 0; p < result.pieces.size(); ++p )
            {
                if( PointInPolygon( closed[i].front(), result.pieces[p].outer ) )
                {
                    double area = std::abs( SignedArea( result.pieces[p].outer ) );

                    if( area < bestArea )
                    {
                        bestArea = area;
                        bestPiece = static_cast<int>( p );
                    }
                }
            }

            if( bestPiece >= 0 )
                result.pieces[bestPiece].holes.push_back( closed[i] );
        }
    }

    result.fromEdgeCuts = true;
    return result;
}


Box2 BoardBoundingBox( const pcb::Board& aBoard, bool aHaveOutline )
{
    Box2 box;

    if( aHaveOutline )
    {
        for( const Shape* s : EdgeCutShapes( aBoard ) )
            box.merge( ShapeBoundingBox( *s ) );
    }
    else
    {
        // Without an outline KiCad uses all items on physical layers.
        auto physical = []( const std::string& layer )
        {
            return layer.find( ".Cu" ) != std::string::npos || layer.find( "SilkS" ) != std::string::npos
                   || layer.find( "Mask" ) != std::string::npos
                   || layer.find( "Paste" ) != std::string::npos
                   || layer.find( "Adhes" ) != std::string::npos || layer == "Edge.Cuts";
        };

        for( const Shape& s : aBoard.drawings )
        {
            if( physical( s.layer ) )
                box.merge( ShapeBoundingBox( s ) );
        }

        for( const pcb::Footprint& fp : aBoard.footprints )
        {
            for( const Shape& s : fp.graphics )
            {
                if( physical( s.layer ) )
                    box.merge( ShapeBoundingBox( s ) );
            }

            for( const pcb::Pad& pad : fp.pads )
            {
                double r = std::max( pad.size.x, pad.size.y ) / 2.0;
                Box2   pb;
                pb.merge( pad.position );
                pb.inflate( r );
                box.merge( pb );
            }
        }

        for( const pcb::Track& t : aBoard.tracks )
        {
            Box2 tb;
            tb.merge( t.start );
            tb.merge( t.end );

            if( t.isArc )
                tb.merge( arcBoundingBox( t.start, t.mid, t.end ) );

            tb.inflate( t.width / 2.0 );
            box.merge( tb );
        }

        for( const pcb::Via& v : aBoard.vias )
        {
            Box2 vb;
            vb.merge( v.position );
            vb.inflate( v.size / 2.0 );
            box.merge( vb );
        }
    }

    // KiCad gives an empty board a 10 mm box so zoom calculations still work.
    if( !box.valid() || ( box.size().x == 0 && box.size().y == 0 ) )
    {
        if( !box.valid() )
            box.merge( Vec2( 0, 0 ) );

        box.inflate( 10.0 );
    }

    return box;
}

} // namespace kirend
