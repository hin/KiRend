#include "output/svg_writer.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace kirend::output {

namespace {

void appendNumber( std::string& aOut, double aValue )
{
    // Two decimals is 1/100 px, plenty for line art and keeps files small.
    double rounded = std::round( aValue * 100.0 ) / 100.0;

    if( rounded == 0.0 )
        rounded = 0.0; // no "-0"

    char buf[32];
    auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), rounded, std::chars_format::fixed, 2 );
    std::string_view text( buf, ptr - buf );

    // Trim trailing zeros: "12.50" -> "12.5", "3.00" -> "3"
    while( text.find( '.' ) != std::string_view::npos && ( text.back() == '0' || text.back() == '.' ) )
    {
        const bool dot = text.back() == '.';
        text.remove_suffix( 1 );

        if( dot )
            break;
    }

    aOut.append( text );
}


void appendPaths( std::string& aOut, const std::vector<render::Polyline2D>& aLines, bool aOutline,
                  style::LineCategory aCategory )
{
    for( const render::Polyline2D& line : aLines )
    {
        if( line.outline != aOutline || line.category != aCategory || line.points.size() < 2 )
            continue;

        aOut += "<path d=\"M";

        for( size_t i = 0; i < line.points.size(); ++i )
        {
            if( i == 1 )
                aOut += 'L';
            else if( i > 1 )
                aOut += ' ';

            appendNumber( aOut, line.points[i].x );
            aOut += ' ';
            appendNumber( aOut, line.points[i].y );
        }

        aOut += "\"/>\n";
    }
}

} // namespace


std::string WriteSvg( const std::vector<render::Polyline2D>& aLines, int aWidth, int aHeight,
                      const style::Style& aStyle, double aDpi )
{
    std::string out;
    out.reserve( 1024 + aLines.size() * 64 );

    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + std::to_string( aWidth )
           + "\" height=\"" + std::to_string( aHeight ) + "\" viewBox=\"0 0 " + std::to_string( aWidth )
           + " " + std::to_string( aHeight ) + "\">\n";

    if( aStyle.background )
        out += "<rect width=\"100%\" height=\"100%\" fill=\"" + aStyle.background->hex() + "\"/>\n";

    auto hasLines = [&]( bool aOutline, style::LineCategory aCategory )
    {
        return std::any_of( aLines.begin(), aLines.end(),
                            [&]( const render::Polyline2D& l )
                            { return l.outline == aOutline && l.category == aCategory && l.points.size() >= 2; } );
    };

    // One group per line category and kind, e.g. "models-edges" and "models-outlines". Board
    // layer categories only have edges. Outlines are drawn last, on top of the edges.
    for( bool outline : { false, true } )
    {
        for( size_t i = 0; i < style::LINE_CATEGORY_COUNT; ++i )
        {
            const auto category = static_cast<style::LineCategory>( i );

            if( !hasLines( outline, category ) )
                continue;

            const style::CategoryStrokes& strokes = aStyle.strokes( category );
            const style::Stroke&          stroke = outline ? strokes.outline : strokes.edge;

            const bool hasKinds = category == style::LineCategory::Board || category == style::LineCategory::Models;

            out += "<g id=\"";
            out += style::CategoryName( category );

            if( hasKinds )
                out += outline ? "-outlines" : "-edges";

            out += "\" fill=\"none\" stroke=\"" + stroke.color.hex() + "\" stroke-width=\"";
            appendNumber( out, style::PointsToPixels( stroke.widthPt, aDpi ) );
            out += "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
            appendPaths( out, aLines, outline, category );
            out += "</g>\n";
        }
    }

    out += "</svg>\n";
    return out;
}

} // namespace kirend::output
