#include "cli/options.h"

#include "sexpr/sexpr.h"

#include <argparse/argparse.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace kirend::cli {

namespace {

std::string trim( const std::string& aText )
{
    size_t a = aText.find_first_not_of( " \t" );
    size_t b = aText.find_last_not_of( " \t" );
    return a == std::string::npos ? std::string() : aText.substr( a, b - a + 1 );
}


std::vector<std::string> split( const std::string& aText, char aSep )
{
    std::vector<std::string> parts;
    std::stringstream        ss( aText );
    std::string              part;

    while( std::getline( ss, part, aSep ) )
        parts.push_back( part );

    return parts;
}


std::string toLower( std::string s )
{
    std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return s;
}

} // namespace


std::optional<Vec3> ParseVector3( const std::string& aText )
{
    std::string text = aText;

    if( !text.empty() && text.front() == '\'' )
        text.erase( 0, 1 );

    if( !text.empty() && text.back() == '\'' )
        text.pop_back();

    std::vector<std::string> parts = split( text, ',' );

    if( parts.size() != 3 )
        return std::nullopt;

    Vec3 v;
    double* out[3] = { &v.x, &v.y, &v.z };

    for( int i = 0; i < 3; ++i )
    {
        std::optional<double> value = sexpr::ToNumber( trim( parts[i] ) );

        if( !value )
            return std::nullopt;

        *out[i] = *value;
    }

    return v;
}


ParseResult ParseCommandLine( const std::vector<std::string>& aArgs, std::ostream& aOut, std::ostream& aErr )
{
    ParseResult result;

    // -h is the image height (as in kicad-cli), so help is only --help.
    argparse::ArgumentParser parser( "kirend", KIREND_VERSION, argparse::default_arguments::none );
    parser.add_description( "Renders a KiCad PCB as line art (SVG or PNG), with the same camera "
                            "options as 'kicad-cli pcb render'." );

    parser.add_argument( "--help" ).flag().help( "Show this help message and exit" );
    parser.add_argument( "--version" ).flag().help( "Print version information and exit" );

    parser.add_argument( "-o", "--output" )
            .default_value( std::string() )
            .metavar( "OUTPUT_FILE" )
            .help( "Output file name (.svg or .png)" );

    parser.add_argument( "-D", "--define-var" )
            .default_value( std::vector<std::string>() )
            .append()
            .metavar( "KEY=VALUE" )
            .help( "Overrides or adds project variables, can be used multiple times to declare "
                   "multiple variables.\nUse in the format of '--define-var key=value' or "
                   "'-D key=value'" );

    parser.add_argument( "-w", "--width" ).default_value( 1600 ).scan<'i', int>().metavar( "WIDTH" ).help( "Image width" );
    parser.add_argument( "-h", "--height" ).default_value( 900 ).scan<'i', int>().metavar( "HEIGHT" ).help( "Image height" );

    parser.add_argument( "--side" )
            .default_value( std::string( "top" ) )
            .choices( "top", "bottom", "left", "right", "front", "back" )
            .metavar( "SIDE" )
            .help( "Render from side. Options: top, bottom, left, right, front, back" );

    parser.add_argument( "--background" )
            .default_value( std::string() )
            .metavar( "BG" )
            .help( "Image background. Options: default (from the style sheet, white), transparent, "
                   "opaque" );

    parser.add_argument( "--perspective" ).flag().help( "Use perspective projection instead of orthogonal" );

    parser.add_argument( "--zoom" ).default_value( 1.0 ).scan<'g', double>().metavar( "ZOOM" ).help( "Camera zoom" );

    parser.add_argument( "--pan" )
            .default_value( std::string() )
            .metavar( "VECTOR" )
            .help( "Pan camera, format 'X,Y,Z' e.g.: '3,0,0'" );

    parser.add_argument( "--pivot" )
            .default_value( std::string() )
            .metavar( "PIVOT" )
            .help( "Set pivot point relative to the board center in centimeters, format 'X,Y,Z' "
                   "e.g.: '-10,2,0'" );

    parser.add_argument( "--rotate" )
            .default_value( std::string() )
            .metavar( "ANGLES" )
            .help( "Rotate board, format 'X,Y,Z' e.g.: '-45,0,45' for isometric view" );

    parser.add_argument( "--style" )
            .default_value( std::string() )
            .metavar( "STYLE_FILE" )
            .help( "YAML style sheet with line widths, colours and what is drawn. Settings it "
                   "leaves out keep their default; options on the command line win over it" );

    parser.add_argument( "-l", "--layers" )
            .default_value( std::string() )
            .metavar( "LAYER_LIST" )
            .help( "Comma separated list of layers to draw. Edge.Cuts draws the board body; copper "
                   "layers draw pads, tracks and vias; other layers draw their graphics. "
                   "Default: from the style sheet (Edge.Cuts)" );

    parser.add_argument( "--dpi" )
            .default_value( 300.0 )
            .scan<'g', double>()
            .metavar( "DPI" )
            .help( "Resolution stored in PNG files, so documents place them at the right size. Also "
                   "converts the style's line widths from points to pixels. Default: 300" );

    parser.add_argument( "-v", "--verbose" ).flag().help( "Print progress and resolved model paths" );

    parser.add_argument( "INPUT_FILE" ).default_value( std::string() ).help( "Input file" );

    // Values like "-45,0,45" start with '-' and would be taken for options. kicad-cli accepts them
    // (also wrapped in single quotes), so wrap them in quotes here; ParseVector3 strips them.
    std::vector<std::string> args;

    for( size_t i = 0; i < aArgs.size(); ++i )
    {
        const std::string& arg = aArgs[i];

        if( ( arg == "--rotate" || arg == "--pan" || arg == "--pivot" ) && i + 1 < aArgs.size()
            && aArgs[i + 1].rfind( "-", 0 ) == 0 )
        {
            args.push_back( arg );
            args.push_back( "'" + aArgs[++i] + "'" );
        }
        else if( arg.rfind( "--rotate=", 0 ) == 0 || arg.rfind( "--pan=", 0 ) == 0
                 || arg.rfind( "--pivot=", 0 ) == 0 )
        {
            size_t eq = arg.find( '=' );
            args.push_back( arg.substr( 0, eq ) );
            args.push_back( "'" + arg.substr( eq + 1 ) + "'" );
        }
        else
        {
            args.push_back( arg );
        }
    }

    try
    {
        parser.parse_args( args );
    }
    catch( const std::exception& e )
    {
        aErr << e.what() << "\n\n" << parser;
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    if( parser.get<bool>( "--help" ) )
    {
        aOut << parser;
        return result;
    }

    if( parser.get<bool>( "--version" ) )
    {
        aOut << KIREND_VERSION << "\n";
        return result;
    }

    RenderJob job;
    job.input = parser.get<std::string>( "INPUT_FILE" );

    if( job.input.empty() )
    {
        aErr << "Input file is required\n\n" << parser;
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    job.output = parser.get<std::string>( "--output" );

    std::string ext = toLower( job.output.extension().string() );

    if( job.output.empty() || ext == ".png" )
        job.format = OutputFormat::Png;
    else if( ext == ".svg" )
        job.format = OutputFormat::Svg;
    else
    {
        aErr << "Unknown output format '" << ext << "', use .svg or .png\n";
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    for( const std::string& def : parser.get<std::vector<std::string>>( "--define-var" ) )
    {
        size_t eq = def.find( '=' );

        if( eq == std::string::npos || eq == 0 )
        {
            aErr << "Invalid variable definition '" << def << "', use KEY=VALUE\n";
            result.exitCode = exit_code::ERR_ARGS;
            return result;
        }

        job.defines[def.substr( 0, eq )] = def.substr( eq + 1 );
    }

    job.camera.width = parser.get<int>( "--width" );
    job.camera.height = parser.get<int>( "--height" );

    // KiCad's ray tracer needs at least a couple of 32-pixel blocks in each direction.
    if( job.camera.width < 64 || job.camera.height < 64 || job.camera.width > 32768 || job.camera.height > 32768 )
    {
        aErr << "Invalid image dimensions (64 to 32768 pixels)\n";
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    const std::string side = parser.get<std::string>( "--side" );

    if( side == "top" )
        job.camera.side = render::Side::Top;
    else if( side == "bottom" )
        job.camera.side = render::Side::Bottom;
    else if( side == "left" )
        job.camera.side = render::Side::Left;
    else if( side == "right" )
        job.camera.side = render::Side::Right;
    else if( side == "front" )
        job.camera.side = render::Side::Front;
    else
        job.camera.side = render::Side::Back;

    const std::string bg = toLower( parser.get<std::string>( "--background" ) );

    if( bg.empty() || bg == "default" )
        job.background = BackgroundStyle::Default;
    else if( bg == "transparent" )
        job.background = BackgroundStyle::Transparent;
    else if( bg == "opaque" )
        job.background = BackgroundStyle::Opaque;
    else
    {
        aErr << "Invalid background\n";
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    job.camera.perspective = parser.get<bool>( "--perspective" );
    job.camera.zoom = parser.get<double>( "--zoom" );

    auto vectorArg = [&]( const char* aName, const char* aError, Vec3& aOut ) -> bool
    {
        const std::string text = parser.get<std::string>( aName );

        if( text.empty() )
            return true;

        if( std::optional<Vec3> v = ParseVector3( text ) )
        {
            aOut = *v;
            return true;
        }

        aErr << aError << "\n";
        return false;
    };

    if( !vectorArg( "--rotate", "Invalid rotation format", job.camera.rotation )
        || !vectorArg( "--pan", "Invalid pan format", job.camera.pan )
        || !vectorArg( "--pivot", "Invalid pivot format", job.camera.pivot ) )
    {
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    job.styleFile = parser.get<std::string>( "--style" );
    job.layers.clear();

    for( const std::string& layer : split( parser.get<std::string>( "--layers" ), ',' ) )
    {
        std::string name = trim( layer );

        if( !name.empty() )
            job.layers.push_back( name );
    }

    job.dpi = parser.get<double>( "--dpi" );

    if( job.dpi <= 0 )
    {
        aErr << "Invalid DPI\n";
        result.exitCode = exit_code::ERR_ARGS;
        return result;
    }

    job.verbose = parser.get<bool>( "--verbose" );
    result.job = std::move( job );
    return result;
}

} // namespace kirend::cli
