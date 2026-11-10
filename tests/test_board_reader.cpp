#include "pcb/board_reader.h"

#include <gtest/gtest.h>

using namespace kirend;
using namespace kirend::pcb;

namespace {

Board loadSimple() { return ReadBoardFile( std::string( KIREND_TEST_DATA ) + "/simple.kicad_pcb" ); }

} // namespace


TEST( BoardReader, ReadsGeneralInfo )
{
    Board board = loadSimple();

    EXPECT_EQ( board.version, 20260206 );
    EXPECT_TRUE( board.warnings.empty() );
    EXPECT_EQ( board.copperLayerCount, 2 );
    EXPECT_TRUE( board.stackup.present );
    EXPECT_NEAR( board.stackup.dielectricThickness, 1.51, 1e-9 );
    EXPECT_EQ( board.layerAliases.at( "F.Silkscreen" ), "F.SilkS" );
}


TEST( BoardReader, ReadsFootprintsAndPads )
{
    Board board = loadSimple();
    ASSERT_EQ( board.footprints.size(), 1u );

    const Footprint& fp = board.footprints[0];
    EXPECT_EQ( fp.reference, "R1" );
    EXPECT_TRUE( fp.smd );
    EXPECT_FALSE( fp.flipped );
    EXPECT_DOUBLE_EQ( fp.angle, 90.0 );

    ASSERT_EQ( fp.pads.size(), 2u );

    // Pad positions are footprint-local, rotated by the footprint angle (KiCad: Y down,
    // positive angles counter-clockwise on screen): local (-0.8, 0) at 90 deg -> (0, +0.8).
    EXPECT_NEAR( fp.pads[0].position.x, 50.0, 1e-9 );
    EXPECT_NEAR( fp.pads[0].position.y, 40.8, 1e-9 );
    EXPECT_EQ( fp.pads[0].shape, Pad::ShapeKind::RoundRect );
    EXPECT_DOUBLE_EQ( fp.pads[0].roundRectRatio, 0.25 );

    EXPECT_EQ( fp.pads[1].type, Pad::Type::ThroughHole );
    EXPECT_DOUBLE_EQ( fp.pads[1].drill.sizeX, 0.6 );
    EXPECT_EQ( fp.pads[1].layers.front(), "F.Cu" );
    EXPECT_EQ( fp.pads[1].layers[1], "B.Cu" );

    ASSERT_EQ( fp.models.size(), 1u );
    EXPECT_EQ( fp.models[0].path, "${KIPRJMOD}/models/part.wrl" );
    EXPECT_DOUBLE_EQ( fp.models[0].offset.z, 0.5 );
    EXPECT_DOUBLE_EQ( fp.models[0].rotation.z, 90.0 );

    // Footprint graphics are converted to board coordinates.
    ASSERT_EQ( fp.graphics.size(), 1u );
    EXPECT_NEAR( fp.graphics[0].start.x, 49.5, 1e-9 );
    EXPECT_NEAR( fp.graphics[0].start.y, 41.0, 1e-9 );
}


TEST( BoardReader, ReadsTracksViasAndDrawings )
{
    Board board = loadSimple();

    ASSERT_EQ( board.tracks.size(), 1u );
    EXPECT_DOUBLE_EQ( board.tracks[0].width, 0.25 );
    ASSERT_EQ( board.vias.size(), 1u );
    EXPECT_DOUBLE_EQ( board.vias[0].drill, 0.3 );
    EXPECT_EQ( board.drawings.size(), 6u ); // text is skipped
}


TEST( BoardReader, WarnsAboutOldFiles )
{
    Board board = ParseBoard( "(kicad_pcb (version 20241229) (generator \"pcbnew\"))" );
    ASSERT_EQ( board.warnings.size(), 1u );
}


TEST( BoardReader, RejectsOtherFiles )
{
    EXPECT_THROW( ParseBoard( "(kicad_sch (version 20250114))" ), BoardReadError );
    EXPECT_THROW( ParseBoard( "not a board" ), BoardReadError );
}


TEST( BoardReader, ExpandsLayerWildcards )
{
    std::vector<std::string> cu = ExpandLayerWildcard( "*.Cu", 4 );
    ASSERT_EQ( cu.size(), 4u );
    EXPECT_EQ( cu[1], "In1.Cu" );
    EXPECT_EQ( cu[3], "B.Cu" );

    std::vector<std::string> mask = ExpandLayerWildcard( "F&B.Mask", 2 );
    ASSERT_EQ( mask.size(), 2u );
    EXPECT_EQ( mask[0], "F.Mask" );
}
