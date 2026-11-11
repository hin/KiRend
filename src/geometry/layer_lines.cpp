#include "geometry/layer_lines.h"

#include "geometry/arc.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kirend {

using pcb::Pad;
using pcb::Shape;

namespace {

const std::map<std::string, std::string>& standardAliases()
{
    static const std::map<std::string, std::string> aliases = {
        { "F.Silkscreen", "F.SilkS" }, { "B.Silkscreen", "B.SilkS" },
        { "F.Adhesive", "F.Adhes" },   { "B.Adhesive", "B.Adhes" },
        { "F.Courtyard", "F.CrtYd" },  { "B.Courtyard", "B.CrtYd" },
        { "User.Drawings", "Dwgs.User" }, { "User.Comments", "Cmts.User" },
        { "User.Eco1", "Eco1.User" },  { "User.Eco2", "Eco2.User" },
    };
    return aliases;
}


bool isKnownLayer( const std::string& aName, const pcb::Board& aBoard )
{
    if( aName == "Edge.Cuts" )
        return true;

    for( const auto& [user, canonical] : aBoard.layerAliases )
    {
        if( canonical == aName )
            return true;
    }

    static const std::set<std::string> standard = {
        "F.Cu", "B.Cu", "F.Adhes", "B.Adhes", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS",
        "F.Mask", "B.Mask", "Dwgs.User", "Cmts.User", "Eco1.User", "Eco2.User", "Edge.Cuts",
        "Margin", "F.CrtYd", "B.CrtYd", "F.Fab", "B.Fab"
    };

    if( standard.count( aName ) )
        return true;

    // Inner copper and numbered user layers.
    if( aName.rfind( "In", 0 ) == 0 && aName.size() > 5 && aName.compare( aName.size() - 3, 3, ".Cu" ) == 0 )
        return true;

    return aName.rfind( "User.", 0 ) == 0;
}


bool isBackLayer( const std::string& aLayer ) { return aLayer.rfind( "B.", 0 ) == 0; }


bool isCopperLayer( const std::string& aLayer )
{
    return aLayer.size() > 3 && aLayer.compare( aLayer.size() - 3, 3, ".Cu" ) == 0;
}


double layerZ( const std::string& aLayer, const pcb::Board& aBoard, const BoardHeights& aHeights )
{
    // Lines are drawn on the outer face of each layer (see BOARD_ADAPTER layer z positions).
    if( aLayer == "F.Cu" )
        return aHeights.copperTopFront;

    if( aLayer == "B.Cu" )
        return aHeights.copperTopBack;

    if( isCopperLayer( aLayer ) )
    {
        // Inner layers: evenly spaced inside the body.
        int index = std::atoi( aLayer.c_str() + 2 );
        int count = std::max( 2, aBoard.copperLayerCount );
        return aHeights.bodyTop - ( aHeights.bodyTop - aHeights.bodyBottom ) * index / ( count - 1 );
    }

    if( aLayer == "F.SilkS" )
        return aHeights.silkFront;

    if( aLayer == "B.SilkS" )
        return aHeights.silkBack;

    // Other technical / user layers sit just outside the silkscreen.
    constexpr double TECH_LAYER = 0.025 * 2.1;
    return isBackLayer( aLayer ) ? aHeights.silkBack - TECH_LAYER : aHeights.silkFront + TECH_LAYER;
}


/// Outline of a stroke of width aWidth along a polyline (round caps).
Polygon strokeOutline( const std::vector<Vec2>& aLine, double aWidth )
{
    const double r = aWidth / 2.0;

    if( aLine.size() < 2 || r <= 0 )
        return {};

    std::vector<Vec2> left;
    std::vector<Vec2> right;

    for( size_t i = 0; i + 1 < aLine.size(); ++i )
    {
        Vec2 d = aLine[i + 1] - aLine[i];
        double len = d.length();

        if( len <= 0 )
            continue;

        Vec2 n( -d.y / len * r, d.x / len * r );
        left.push_back( aLine[i] + n );
        left.push_back( aLine[i + 1] + n );
        right.push_back( aLine[i] - n );
        right.push_back( aLine[i + 1] - n );
    }

    if( left.empty() )
        return ApproximateCircle( aLine.front(), r );

    Polygon poly = left;

    // End cap: half circle from left end to right end around the last point.
    {
        const Vec2 c = aLine.back();
        const Vec2 a = left.back();
        const Vec2 b = right.back();
        const Vec2 d = ( a - c );
        const Vec2 mid = c + Vec2( d.y, -d.x );
        std::vector<Vec2> cap = ApproximateArc( a, mid, b );
        poly.insert( poly.end(), cap.begin() + 1, cap.end() - 1 );
    }

    poly.insert( poly.end(), right.rbegin(), right.rend() );

    {
        const Vec2 c = aLine.front();
        const Vec2 a = right.front();
        const Vec2 b = left.front();
        const Vec2 d = ( a - c );
        const Vec2 mid = c + Vec2( d.y, -d.x );
        std::vector<Vec2> cap = ApproximateArc( a, mid, b );
        poly.insert( poly.end(), cap.begin() + 1, cap.end() - 1 );
    }

    return poly;
}


Polygon roundRectPolygon( const Vec2& aSize, double aRadius, double aChamfer, int aChamferCorners )
{
    const double hx = aSize.x / 2, hy = aSize.y / 2;
    const double r = std::min( { aRadius, hx, hy } );
    const double ch = std::min( { aChamfer, aSize.x, aSize.y } );

    // Corners in pad-local coordinates (Y down): TL, TR, BR, BL.
    struct Corner
    {
        Vec2 pos;
        Vec2 dirIn;  ///< direction towards the previous corner
        Vec2 dirOut; ///< direction towards the next corner
        int  bit;
    };

    const Corner corners[4] = {
        { { -hx, -hy }, { 0, 1 }, { 1, 0 }, 1 },   // top left
        { { hx, -hy }, { -1, 0 }, { 0, 1 }, 2 },   // top right
        { { hx, hy }, { 0, -1 }, { -1, 0 }, 8 },   // bottom right
        { { -hx, hy }, { 1, 0 }, { 0, -1 }, 4 },   // bottom left
    };

    Polygon poly;

    for( const Corner& c : corners )
    {
        if( ( aChamferCorners & c.bit ) && ch > 0 )
        {
            poly.push_back( c.pos + c.dirIn * ch );
            poly.push_back( c.pos + c.dirOut * ch );
        }
        else if( r > 0 )
        {
            const Vec2 a = c.pos + c.dirIn * r;
            const Vec2 b = c.pos + c.dirOut * r;
            const Vec2 center = c.pos + c.dirIn * r + c.dirOut * r;
            const Vec2 mid = center + ( c.pos - center ) / ( c.pos - center ).length() * r;
            std::vector<Vec2> arc = ApproximateArc( a, mid, b );
            poly.insert( poly.end(), arc.begin(), arc.end() );
        }
        else
        {
            poly.push_back( c.pos );
        }
    }

    return poly;
}


void addShapeLines( const Shape& aShape, double aZ, render::LineSet& aOut )
{
    auto lift = [&]( const std::vector<Vec2>& aPoints, bool aClose )
    {
        std::vector<Vec3> line;

        for( const Vec2& p : aPoints )
            line.push_back( BoardTo3D( p, aZ ) );

        if( aClose && !aPoints.empty() )
            line.push_back( BoardTo3D( aPoints.front(), aZ ) );

        if( line.size() >= 2 )
            aOut.polylines.push_back( std::move( line ) );
    };

    // Graphics are drawn as centre lines; filled shapes as their outline.
    if( std::optional<Polygon> poly = ClosedShapeToPolygon( aShape ) )
        lift( *poly, true );
    else
        lift( OpenShapeToPolyline( aShape ), false );
}

} // namespace


std::string CanonicalLayerName( const std::string& aName, const pcb::Board& aBoard )
{
    if( auto it = aBoard.layerAliases.find( aName ); it != aBoard.layerAliases.end() )
        return it->second;

    if( auto it = standardAliases().find( aName ); it != standardAliases().end() )
        return it->second;

    return isKnownLayer( aName, aBoard ) ? aName : std::string();
}


std::vector<Polygon> PadPolygons( const Pad& aPad )
{
    std::vector<Polygon> result;
    Polygon              local;

    switch( aPad.shape )
    {
    case Pad::ShapeKind::Circle:
        local = ApproximateCircle( { 0, 0 }, aPad.size.x / 2.0 );
        break;

    case Pad::ShapeKind::Rect:
        local = roundRectPolygon( aPad.size, 0.0, 0.0, 0 );
        break;

    case Pad::ShapeKind::Oval:
    {
        const bool   horizontal = aPad.size.x >= aPad.size.y;
        const double r = std::min( aPad.size.x, aPad.size.y ) / 2.0;
        const double half = std::max( aPad.size.x, aPad.size.y ) / 2.0 - r;
        Vec2         axis = horizontal ? Vec2( half, 0 ) : Vec2( 0, half );
        local = strokeOutline( { axis * -1.0, axis }, 2 * r );

        if( half <= 0 )
            local = ApproximateCircle( { 0, 0 }, r );

        break;
    }

    case Pad::ShapeKind::RoundRect:
    {
        const double radius = aPad.roundRectRatio * std::min( aPad.size.x, aPad.size.y );
        const double chamfer = aPad.chamferRatio * std::min( aPad.size.x, aPad.size.y );
        local = roundRectPolygon( aPad.size, radius, chamfer, aPad.chamferCorners );
        break;
    }

    case Pad::ShapeKind::Trapezoid:
    {
        // Same corner construction as KiCad's TransformShapeToPolygon for trapezoids.
        const double hx = aPad.size.x / 2, hy = aPad.size.y / 2;
        const double ddx = aPad.rectDelta.x / 2, ddy = aPad.rectDelta.y / 2;
        local = { { -hx - ddy, hy + ddx }, { hx + ddy, hy - ddx }, { hx - ddy, -hy + ddx },
                  { -hx + ddy, -hy - ddx } };
        break;
    }

    case Pad::ShapeKind::Custom:
    {
        // Anchor plus each primitive; overlaps are not merged.
        if( aPad.customAnchorCircle )
            result.push_back( ApproximateCircle( { 0, 0 }, aPad.size.x / 2.0 ) );
        else
            result.push_back( roundRectPolygon( aPad.size, 0.0, 0.0, 0 ) );

        for( const Shape& prim : aPad.primitives )
        {
            if( std::optional<Polygon> poly = ClosedShapeToPolygon( prim ) )
            {
                if( prim.width > 0 && !prim.filled )
                    result.push_back( strokeOutline( [&] { Polygon p = *poly; p.push_back( p.front() ); return p; }(), prim.width ) );
                else
                    result.push_back( *poly );
            }
            else if( prim.width > 0 )
            {
                result.push_back( strokeOutline( OpenShapeToPolyline( prim ), prim.width ) );
            }
        }

        break;
    }
    }

    if( !local.empty() )
        result.push_back( std::move( local ) );

    // Pad-local to board: shape offset, pad rotation, pad position.
    for( Polygon& poly : result )
    {
        for( Vec2& p : poly )
            p = RotateKiCad( p + aPad.drill.offset, aPad.angle ) + aPad.position;
    }

    return result;
}


std::vector<render::LineSet> BuildLayerLines( const pcb::Board& aBoard, const std::string& aLayer,
                                              const BoardHeights& aHeights, const LayerContent& aContent )
{
    using style::LineCategory;

    render::LineSet graphics;
    render::LineSet pads;
    render::LineSet tracks;
    render::LineSet vias;
    graphics.category = LineCategory::Graphics;
    pads.category = LineCategory::Pads;
    tracks.category = LineCategory::Tracks;
    vias.category = LineCategory::Vias;

    const double z = layerZ( aLayer, aBoard, aHeights );

    auto addPolygon = [&]( const Polygon& aPoly, render::LineSet& out )
    {
        if( aPoly.size() < 2 )
            return;

        std::vector<Vec3> line;

        for( const Vec2& p : aPoly )
            line.push_back( BoardTo3D( p, z ) );

        line.push_back( BoardTo3D( aPoly.front(), z ) );
        out.polylines.push_back( std::move( line ) );
    };

    if( aContent.graphics )
    {
        for( const Shape& s : aBoard.drawings )
        {
            if( s.layer == aLayer )
                addShapeLines( s, z, graphics );
        }
    }

    for( const pcb::Footprint& fp : aBoard.footprints )
    {
        if( aContent.graphics )
        {
            for( const Shape& s : fp.graphics )
            {
                if( s.layer == aLayer )
                    addShapeLines( s, z, graphics );
            }
        }

        if( !isCopperLayer( aLayer ) || !aContent.pads )
            continue;

        for( const Pad& pad : fp.pads )
        {
            if( std::find( pad.layers.begin(), pad.layers.end(), aLayer ) == pad.layers.end() )
                continue;

            for( const Polygon& poly : PadPolygons( pad ) )
                addPolygon( poly, pads );
        }
    }

    if( isCopperLayer( aLayer ) )
    {
        for( const pcb::Track& t : aBoard.tracks )
        {
            if( !aContent.tracks || t.layer != aLayer )
                continue;

            std::vector<Vec2> path = t.isArc ? ApproximateArc( t.start, t.mid, t.end )
                                             : std::vector<Vec2>{ t.start, t.end };
            addPolygon( strokeOutline( path, t.width ), tracks );
        }

        for( const pcb::Via& v : aBoard.vias )
        {
            // Through vias list "F.Cu" "B.Cu"; they appear on both outer layers.
            bool onLayer = aContent.vias
                           && ( v.layers.empty()
                                || std::find( v.layers.begin(), v.layers.end(), aLayer ) != v.layers.end() );

            if( onLayer )
                addPolygon( ApproximateCircle( v.position, v.size / 2.0 ), vias );
        }
    }

    return { std::move( graphics ), std::move( pads ), std::move( tracks ), std::move( vias ) };
}

} // namespace kirend
