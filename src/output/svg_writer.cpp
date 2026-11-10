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


void appendPaths( std::string& aOut, const std::vector<render::Polyline2D>& aLines, bool aOutline )
{
    for( const render::Polyline2D& line : aLines )
    {
        if( line.outline != aOutline || line.points.size() < 2 )
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


SvgStyle DefaultStyle( int aImageHeight )
{
    SvgStyle style;
    style.lineWidth = std::max( 1.0, aImageHeight / 600.0 );
    style.outlineWidth = style.lineWidth * 2.0;
    return style;
}


std::string WriteSvg( const std::vector<render::Polyline2D>& aLines, int aWidth, int aHeight,
                      const SvgStyle& aStyle )
{
    std::string out;
    out.reserve( 1024 + aLines.size() * 64 );

    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + std::to_string( aWidth )
           + "\" height=\"" + std::to_string( aHeight ) + "\" viewBox=\"0 0 " + std::to_string( aWidth )
           + " " + std::to_string( aHeight ) + "\">\n";

    if( aStyle.background == Background::White )
        out += "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";

    auto group = [&]( const char* aId, double aWidthPx, bool aOutline )
    {
        out += "<g id=\"";
        out += aId;
        out += "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"";
        appendNumber( out, aWidthPx );
        out += "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
        appendPaths( out, aLines, aOutline );
        out += "</g>\n";
    };

    group( "edges", aStyle.lineWidth, false );
    group( "outlines", aStyle.outlineWidth, true );

    out += "</svg>\n";
    return out;
}

} // namespace kirend::output
