#include "geometry/board_geometry.h"

#include "geometry/arc.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace kirend {

BoardHeights ComputeBoardHeights( const pcb::Board& aBoard )
{
    // Defaults from board_adapter.cpp
    constexpr double DEFAULT_COPPER = 0.035;
    constexpr double DEFAULT_TECH_LAYER = 0.025;
    constexpr double SOLDERPASTE_LAYER = 0.04;
    constexpr double DEFAULT_MASK = 0.01; // BOARD_STACKUP_ITEM::GetMaskDefaultThickness()
    constexpr double THICKNESS_MARGIN = 1.1;

    double body;
    double frontCopper = DEFAULT_COPPER;
    double backCopper = DEFAULT_COPPER;

    if( aBoard.stackup.present )
    {
        body = aBoard.stackup.dielectricThickness;
        frontCopper = aBoard.stackup.frontCopper;
        backCopper = aBoard.stackup.backCopper;
    }
    else
    {
        // KiCad builds a default stackup from the board thickness (BuildDefaultStackupList):
        // the dielectric fills what the copper and two mask layers leave.
        const int n = aBoard.copperLayerCount;
        body = aBoard.generalThickness - DEFAULT_COPPER * n - DEFAULT_MASK * 2 + DEFAULT_COPPER * ( n - 2 );
    }

    BoardHeights h;
    h.bodyTop = body / 2.0;
    h.bodyBottom = -body / 2.0;
    h.copperTopFront = h.bodyTop + frontCopper;
    h.copperTopBack = h.bodyBottom - backCopper;

    const double offset = DEFAULT_TECH_LAYER * THICKNESS_MARGIN;
    h.silkFront = h.copperTopFront + offset + DEFAULT_TECH_LAYER;
    h.silkBack = h.copperTopBack - offset - DEFAULT_TECH_LAYER;

    // BOARD_ADAPTER::GetFootprintZPos(): top of F.Paste, or the bottom z of B.Paste.
    h.footprintFront = h.copperTopFront + SOLDERPASTE_LAYER;
    h.footprintBack = h.copperTopBack;
    return h;
}


std::vector<DrillHole> CollectDrillHoles( const pcb::Board& aBoard )
{
    std::vector<DrillHole> holes;

    for( const pcb::Footprint& fp : aBoard.footprints )
    {
        for( const pcb::Pad& pad : fp.pads )
        {
            if( !pad.drill.present() )
                continue;

            if( pad.type != pcb::Pad::Type::ThroughHole && pad.type != pcb::Pad::Type::NpThroughHole )
                continue;

            DrillHole hole;
            hole.center = pad.position;
            hole.sizeX = pad.drill.sizeX;
            hole.sizeY = pad.drill.oval ? pad.drill.sizeY : pad.drill.sizeX;
            hole.angle = pad.angle;
            holes.push_back( hole );
        }
    }

    for( const pcb::Via& via : aBoard.vias )
    {
        if( via.drill <= 0 )
            continue;

        DrillHole hole;
        hole.center = via.position;
        hole.sizeX = hole.sizeY = via.drill;
        holes.push_back( hole );
    }

    return holes;
}


Polygon DrillHolePolygon( const DrillHole& aHole )
{
    if( std::abs( aHole.sizeX - aHole.sizeY ) < 1e-9 )
        return ApproximateCircle( aHole.center, aHole.sizeX / 2.0 );

    // Slot: a stadium along the longer axis.
    const bool   horizontal = aHole.sizeX > aHole.sizeY;
    const double r = std::min( aHole.sizeX, aHole.sizeY ) / 2.0;
    const double half = std::max( aHole.sizeX, aHole.sizeY ) / 2.0 - r;
    const Vec2   axis = horizontal ? Vec2( 1, 0 ) : Vec2( 0, 1 );
    const Vec2   normal( -axis.y, axis.x );

    Polygon poly;
    const Vec2 c1 = axis * half;
    const Vec2 c2 = axis * -half;

    std::vector<Vec2> arc1 = ApproximateArc( c1 + normal * -r, c1 + axis * r, c1 + normal * r );
    std::vector<Vec2> arc2 = ApproximateArc( c2 + normal * r, c2 + axis * -r, c2 + normal * -r );
    poly.insert( poly.end(), arc1.begin(), arc1.end() );
    poly.insert( poly.end(), arc2.begin(), arc2.end() );

    for( Vec2& p : poly )
        p = RotateKiCad( p, aHole.angle ) + aHole.center;

    return poly;
}


namespace {

/// Closed wire; aCounterClockwise selects the orientation in the XY plane (outer boundaries
/// counter-clockwise, holes clockwise, as OCCT expects for a face with normal +Z).
TopoDS_Wire makeWire( const Polygon& aPoly, bool aCounterClockwise )
{
    BRepBuilderAPI_MakePolygon maker;
    const bool                 isCcw = SignedArea( aPoly ) > 0;

    if( isCcw == aCounterClockwise )
    {
        for( const Vec2& p : aPoly )
            maker.Add( gp_Pnt( p.x, p.y, 0.0 ) );
    }
    else
    {
        for( auto it = aPoly.rbegin(); it != aPoly.rend(); ++it )
            maker.Add( gp_Pnt( it->x, it->y, 0.0 ) );
    }

    maker.Close();
    return maker.Wire();
}


void cleanPolygon( Polygon& aPoly )
{
    // Remove consecutive duplicates; OCCT rejects zero-length polygon edges.
    Polygon out;

    for( const Vec2& p : aPoly )
    {
        if( out.empty() || ( p - out.back() ).length() > 1e-6 )
            out.push_back( p );
    }

    while( out.size() > 1 && ( out.front() - out.back() ).length() <= 1e-6 )
        out.pop_back();

    aPoly = std::move( out );
}


bool polygonInside( const Polygon& aInner, const Polygon& aOuter )
{
    for( const Vec2& p : aInner )
    {
        if( !PointInPolygon( p, aOuter ) )
            return false;
    }

    return true;
}

} // namespace


render::Mesh BuildBoardBodyMesh( const BoardOutline& aOutline, const std::vector<DrillHole>& aHoles,
                                 const BoardHeights& aHeights, std::vector<std::string>& aWarnings )
{
    render::Mesh mesh;

    std::vector<Polygon> holePolys;
    holePolys.reserve( aHoles.size() );

    for( const DrillHole& h : aHoles )
    {
        Polygon poly = DrillHolePolygon( h );
        cleanPolygon( poly );

        if( poly.size() >= 3 )
            holePolys.push_back( std::move( poly ) );
    }

    // Holes that overlap each other are fused into the cutting tool instead of being added as
    // inner wires. Sort by x to find overlaps quickly.
    std::vector<Box2> holeBoxes;

    for( const Polygon& p : holePolys )
    {
        Box2 b;
        for( const Vec2& v : p )
            b.merge( v );
        holeBoxes.push_back( b );
    }

    std::vector<size_t> order( holePolys.size() );

    for( size_t i = 0; i < order.size(); ++i )
        order[i] = i;

    std::sort( order.begin(), order.end(),
               [&]( size_t a, size_t b ) { return holeBoxes[a].min.x < holeBoxes[b].min.x; } );

    std::vector<bool> overlapping( holePolys.size(), false );

    for( size_t i = 0; i < order.size(); ++i )
    {
        const Box2& a = holeBoxes[order[i]];

        for( size_t j = i + 1; j < order.size(); ++j )
        {
            const Box2& b = holeBoxes[order[j]];

            if( b.min.x > a.max.x )
                break;

            if( b.min.y <= a.max.y && b.max.y >= a.min.y )
            {
                overlapping[order[i]] = true;
                overlapping[order[j]] = true;
            }
        }
    }

    for( const OutlinePiece& piece : aOutline.pieces )
    {
        Polygon outer = piece.outer;
        cleanPolygon( outer );

        if( outer.size() < 3 )
            continue;

        std::vector<Polygon> cutouts = piece.holes;

        for( Polygon& c : cutouts )
            cleanPolygon( c );

        BRepBuilderAPI_MakeFace faceMaker( gp_Pln(), makeWire( outer, true ) );

        for( const Polygon& c : cutouts )
        {
            if( c.size() >= 3 )
                faceMaker.Add( makeWire( c, false ) );
        }

        TopoDS_Compound tools;
        BRep_Builder    builder;
        builder.MakeCompound( tools );
        bool haveTools = false;

        for( size_t i = 0; i < holePolys.size(); ++i )
        {
            const Polygon& hp = holePolys[i];

            if( !PointInPolygon( aHoles.size() == holePolys.size() ? aHoles[i].center : hp.front(), outer ) )
                continue;

            bool clean = !overlapping[i] && polygonInside( hp, outer );

            for( const Polygon& c : cutouts )
            {
                if( !clean )
                    break;

                for( const Vec2& v : hp )
                {
                    if( PointInPolygon( v, c ) )
                    {
                        clean = false;
                        break;
                    }
                }
            }

            if( clean )
            {
                faceMaker.Add( makeWire( hp, false ) );
            }
            else
            {
                builder.Add( tools, BRepBuilderAPI_MakeFace( gp_Pln(), makeWire( hp, true ) ).Face() );
                haveTools = true;
            }
        }

        if( !faceMaker.IsDone() )
        {
            aWarnings.push_back( "could not build the board body from its outline" );
            continue;
        }

        TopoDS_Shape face = faceMaker.Face();

        if( haveTools )
        {
            BRepAlgoAPI_Cut cut( face, tools );

            if( cut.IsDone() )
                face = cut.Shape();
            else
                aWarnings.push_back( "could not cut holes that cross the board edge" );
        }

        BRepMesh_IncrementalMesh meshing( face, 0.005, false, 0.1, true );

        // Collect the 2D triangulation of all resulting faces.
        for( TopExp_Explorer exp( face, TopAbs_FACE ); exp.More(); exp.Next() )
        {
            const TopoDS_Face&         f = TopoDS::Face( exp.Current() );
            TopLoc_Location            loc;
            Handle( Poly_Triangulation ) tri = BRep_Tool::Triangulation( f, loc );

            if( tri.IsNull() )
                continue;

            const gp_Trsf trsf = loc.Transformation();
            const uint32_t topBase = static_cast<uint32_t>( mesh.vertices.size() );
            const int      n = tri->NbNodes();

            for( int i = 1; i <= n; ++i )
            {
                gp_Pnt p = tri->Node( i ).Transformed( trsf );
                mesh.vertices.push_back( BoardTo3D( { p.X(), p.Y() }, aHeights.bodyTop ) );
            }

            const uint32_t bottomBase = static_cast<uint32_t>( mesh.vertices.size() );

            for( int i = 1; i <= n; ++i )
            {
                gp_Pnt p = tri->Node( i ).Transformed( trsf );
                mesh.vertices.push_back( BoardTo3D( { p.X(), p.Y() }, aHeights.bodyBottom ) );
            }

            // Count how often each edge is used to find the boundary.
            std::unordered_map<uint64_t, int> edgeUse;

            auto key = []( uint32_t a, uint32_t b )
            {
                if( a > b )
                    std::swap( a, b );
                return ( static_cast<uint64_t>( a ) << 32 ) | b;
            };

            std::vector<std::array<uint32_t, 2>> directed;

            for( int t = 1; t <= tri->NbTriangles(); ++t )
            {
                int a, b, c;
                tri->Triangle( t ).Get( a, b, c );
                const uint32_t ia = a - 1, ib = b - 1, ic = c - 1;

                mesh.triangles.push_back( { topBase + ia, topBase + ib, topBase + ic } );
                mesh.triangles.push_back( { bottomBase + ia, bottomBase + ic, bottomBase + ib } );

                for( auto [u, v] : { std::pair{ ia, ib }, std::pair{ ib, ic }, std::pair{ ic, ia } } )
                {
                    ++edgeUse[key( u, v )];
                    directed.push_back( { u, v } );
                }
            }

            // Side walls along boundary edges.
            for( const auto& [u, v] : directed )
            {
                if( edgeUse[key( u, v )] != 1 )
                    continue;

                const uint32_t tu = topBase + u, tv = topBase + v;
                const uint32_t bu = bottomBase + u, bv = bottomBase + v;
                mesh.triangles.push_back( { tu, bu, bv } );
                mesh.triangles.push_back( { tu, bv, tv } );
            }
        }
    }

    return mesh;
}

} // namespace kirend
