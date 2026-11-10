#include "geometry/board_geometry.h"
#include "geometry/outline.h"
#include "pcb/board_reader.h"

#include <gtest/gtest.h>

using namespace kirend;

TEST( Outline, ChainsSegmentsAndFindsHoles )
{
    pcb::Board   board = pcb::ReadBoardFile( std::string( KIREND_TEST_DATA ) + "/simple.kicad_pcb" );
    BoardOutline outline = BuildBoardOutline( board );

    ASSERT_TRUE( outline.fromEdgeCuts );
    ASSERT_EQ( outline.pieces.size(), 1u );
    EXPECT_EQ( outline.pieces[0].holes.size(), 1u );
}


TEST( Outline, BoundingBoxIncludesStrokeWidth )
{
    pcb::Board board = pcb::ReadBoardFile( std::string( KIREND_TEST_DATA ) + "/simple.kicad_pcb" );
    Box2       box = BoardBoundingBox( board, true );

    EXPECT_NEAR( box.min.x, -0.05, 1e-9 );
    EXPECT_NEAR( box.min.y, -0.05, 1e-9 );
    EXPECT_NEAR( box.max.x, 100.05, 1e-9 );
    EXPECT_NEAR( box.max.y, 90.05, 1e-9 );
}


TEST( Outline, OpenOutlineFallsBack )
{
    pcb::Board board = pcb::ParseBoard( "(kicad_pcb (version 20260206)"
                                        " (gr_line (start 0 0) (end 10 0) (layer \"Edge.Cuts\")))" );
    BoardOutline outline = BuildBoardOutline( board );
    EXPECT_FALSE( outline.fromEdgeCuts );
    EXPECT_FALSE( outline.warning.empty() );
}


TEST( BoardGeometry, HeightsFollowKiCad )
{
    pcb::Board   board = pcb::ReadBoardFile( std::string( KIREND_TEST_DATA ) + "/simple.kicad_pcb" );
    BoardHeights h = ComputeBoardHeights( board );

    EXPECT_NEAR( h.bodyTop, 0.755, 1e-9 );
    EXPECT_NEAR( h.copperTopFront, 0.79, 1e-9 );
    EXPECT_NEAR( h.footprintFront, 0.83, 1e-9 ); // + solder paste layer
    EXPECT_NEAR( h.footprintBack, -0.79, 1e-9 );
}


TEST( BoardGeometry, BodyMeshIsClosed )
{
    pcb::Board               board = pcb::ReadBoardFile( std::string( KIREND_TEST_DATA ) + "/simple.kicad_pcb" );
    BoardOutline             outline = BuildBoardOutline( board );
    std::vector<std::string> warnings;
    render::Mesh mesh = BuildBoardBodyMesh( outline, CollectDrillHoles( board ), ComputeBoardHeights( board ), warnings );

    EXPECT_TRUE( warnings.empty() );
    EXPECT_GT( mesh.triangles.size(), 20u );

    for( const Vec3& v : mesh.vertices )
        EXPECT_TRUE( std::abs( std::abs( v.z ) - 0.755 ) < 1e-9 );
}
