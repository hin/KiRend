#include "style/style.h"

#include <gtest/gtest.h>

#include <string>

using namespace kirend::style;

namespace {

Style apply( const std::string& aText )
{
    Style style = DefaultStyle();
    ApplyStyle( aText, "test.yaml", style );
    return style;
}


std::string errorOf( const std::string& aText )
{
    try
    {
        apply( aText );
    }
    catch( const StyleError& e )
    {
        return e.what();
    }

    return {};
}

} // namespace


TEST( Style, DefaultMatchesPreviousLook )
{
    const Style& s = DefaultStyle();

    ASSERT_TRUE( s.background.has_value() );
    EXPECT_EQ( s.background->hex(), "#ffffff" );
    EXPECT_EQ( s.visible.layers, std::vector<std::string>{ "Edge.Cuts" } );
    EXPECT_TRUE( s.visible.models );
    EXPECT_FALSE( s.visible.dnp );
    EXPECT_TRUE( s.visible.pads );
    EXPECT_TRUE( s.visible.tracks );
    EXPECT_TRUE( s.visible.vias );
    EXPECT_TRUE( s.visible.graphics );

    // Before style sheets, widths were height / 600 and twice that. The default 1600 x 900 window
    // gives an 872 px high image, so 1.45 and 2.91 px; the default style is within 0.01 px of that
    // at 300 DPI.
    const double thin = 872 / 600.0;

    for( LineCategory c : { LineCategory::Board, LineCategory::Models } )
    {
        EXPECT_NEAR( PointsToPixels( s.strokes( c ).outline.widthPt, 300 ), 2 * thin, 0.011 );
        EXPECT_NEAR( PointsToPixels( s.strokes( c ).edge.widthPt, 300 ), thin, 0.011 );
        EXPECT_EQ( s.strokes( c ).outline.color.hex(), "#000000" );
    }

    for( LineCategory c : { LineCategory::Pads, LineCategory::Tracks, LineCategory::Vias, LineCategory::Graphics } )
    {
        EXPECT_NEAR( PointsToPixels( s.strokes( c ).edge.widthPt, 300 ), thin, 0.011 );
        EXPECT_EQ( s.strokes( c ).edge.color.hex(), "#000000" );
    }
}


TEST( Style, EmptyFileKeepsDefaults )
{
    EXPECT_EQ( apply( "" ), DefaultStyle() );
    EXPECT_EQ( apply( "# only a comment\n" ), DefaultStyle() );
}


TEST( Style, PartialFileOnlyChangesWhatItSets )
{
    Style s = apply( "lines:\n"
                     "  models:\n"
                     "    outline: { width: 1.2pt }\n"
                     "  tracks: { color: \"#c00\" }\n"
                     "visible:\n"
                     "  layers: [Edge.Cuts, F.Cu]\n"
                     "  vias: false\n" );

    Style expected = DefaultStyle();
    expected.strokes( LineCategory::Models ).outline.widthPt = 1.2;
    expected.strokes( LineCategory::Tracks ).edge.color = Color{ 0xcc, 0, 0 };
    expected.visible.layers = { "Edge.Cuts", "F.Cu" };
    expected.visible.vias = false;

    EXPECT_EQ( s, expected );
}


TEST( Style, WidthsAndColours )
{
    Style s = apply( "lines:\n"
                     "  pads: { width: 0.5, color: \"#12AbEf\" }\n"
                     "  vias: { width: 0.25 pt }\n" );

    EXPECT_DOUBLE_EQ( s.strokes( LineCategory::Pads ).edge.widthPt, 0.5 );
    EXPECT_EQ( s.strokes( LineCategory::Pads ).edge.color, ( Color{ 0x12, 0xab, 0xef } ) );
    EXPECT_DOUBLE_EQ( s.strokes( LineCategory::Vias ).edge.widthPt, 0.25 );
    EXPECT_DOUBLE_EQ( PointsToPixels( 0.72, 300 ), 3.0 );
}


TEST( Style, Background )
{
    EXPECT_FALSE( apply( "background: transparent\n" ).background.has_value() );
    EXPECT_EQ( apply( "background: \"#eeeeee\"\n" ).background, ( Color{ 0xee, 0xee, 0xee } ) );
}


TEST( Style, Errors )
{
    EXPECT_EQ( errorOf( "colour: \"#000\"\n" ).rfind( "test.yaml:1:1: unknown setting 'colour'", 0 ), 0u );
    EXPECT_NE( errorOf( "lines:\n  models:\n    outline: { thickness: 1 }\n" ).find( "test.yaml:3:16: unknown setting "
                                                                                      "'lines.models.outline.thickness'" ),
               std::string::npos );
    EXPECT_NE( errorOf( "lines:\n  pads: { width: 0 }\n" ).find( "test.yaml:2:18:" ), std::string::npos );
    EXPECT_NE( errorOf( "lines:\n  pads: { width: 1mm }\n" ).find( "width in points" ), std::string::npos );
    EXPECT_NE( errorOf( "lines:\n  pads: { color: red }\n" ).find( "must be a colour" ), std::string::npos );
    EXPECT_NE( errorOf( "lines:\n  pads:\n    color: #ff0000\n" ).find( "unquoted #" ), std::string::npos );
    EXPECT_NE( errorOf( "visible:\n  vias: maybe\n" ).find( "true or false" ), std::string::npos );
    EXPECT_NE( errorOf( "visible:\n  layers: Edge.Cuts\n" ).find( "must be a list" ), std::string::npos );
    EXPECT_NE( errorOf( "lines: [1, 2]\n" ).find( "'lines' must be a mapping" ), std::string::npos );
    EXPECT_NE( errorOf( "lines:\n  pads: {\n" ).find( "test.yaml:" ), std::string::npos ); // YAML syntax error
    EXPECT_NE( errorOf( "- a\n- b\n" ).find( "must be a mapping" ), std::string::npos );
}


TEST( Style, FailedApplyLeavesStyleUnchanged )
{
    Style s = DefaultStyle();
    EXPECT_THROW( ApplyStyle( "visible: { vias: false, pads: nope }\n", "x", s ), StyleError );
    EXPECT_EQ( s, DefaultStyle() );
}


TEST( Style, MissingFile )
{
    EXPECT_THROW( LoadStyleFile( "/nonexistent/style.yaml" ), StyleError );
}
