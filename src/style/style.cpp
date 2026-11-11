#include "style/style.h"

#include "sexpr/sexpr.h"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace kirend::style {

namespace {

constexpr std::array<const char*, LINE_CATEGORY_COUNT> CATEGORY_NAMES = {
    "board", "models", "pads", "tracks", "vias", "graphics"
};


class Reader
{
public:
    explicit Reader( std::string aSourceName ) : m_source( std::move( aSourceName ) ) {}

    [[noreturn]] void fail( const YAML::Node& aNode, const std::string& aMessage ) const
    {
        const YAML::Mark mark = aNode.Mark();
        fail( mark, aMessage );
    }

    [[noreturn]] void fail( const YAML::Mark& aMark, const std::string& aMessage ) const
    {
        std::string where = m_source;

        if( !aMark.is_null() )
            where += ":" + std::to_string( aMark.line + 1 ) + ":" + std::to_string( aMark.column + 1 );

        throw StyleError( where + ": " + aMessage );
    }

    /// Call aHandlers[key] for each key of the map aNode; unknown keys are errors.
    void forEachKey( const YAML::Node& aNode, const std::string& aPath,
                     const std::map<std::string, std::function<void( const YAML::Node& )>>& aHandlers ) const
    {
        if( aNode.IsNull() )
            return;

        if( !aNode.IsMap() )
            fail( aNode, "'" + aPath + "' must be a mapping of settings" );

        for( const auto& item : aNode )
        {
            const std::string key = item.first.Scalar();
            auto              it = aHandlers.find( key );

            if( it == aHandlers.end() )
            {
                std::string known;

                for( const auto& [name, handler] : aHandlers )
                    known += ( known.empty() ? "" : ", " ) + name;

                fail( item.first, "unknown setting '" + ( aPath.empty() ? key : aPath + "." + key )
                                          + "' (expected one of: " + known + ")" );
            }

            it->second( item.second );
        }
    }

    bool boolean( const YAML::Node& aNode, const std::string& aPath ) const
    {
        if( aNode.IsScalar() )
        {
            const std::string text = aNode.Scalar();

            if( text == "true" || text == "True" || text == "TRUE" )
                return true;

            if( text == "false" || text == "False" || text == "FALSE" )
                return false;
        }

        fail( aNode, "'" + aPath + "' must be true or false" );
    }

    Color color( const YAML::Node& aNode, const std::string& aPath ) const
    {
        if( aNode.IsNull() )
        {
            fail( aNode, "'" + aPath + "' has no value. Write colours in quotes, like \"#000000\": "
                         "an unquoted # starts a comment in YAML" );
        }

        if( aNode.IsScalar() )
        {
            if( std::optional<Color> c = parseColor( aNode.Scalar() ) )
                return *c;
        }

        fail( aNode, "'" + aPath + "' must be a colour like \"#000000\" or \"#000\"" );
    }

    double width( const YAML::Node& aNode, const std::string& aPath ) const
    {
        if( aNode.IsScalar() )
        {
            std::string text = aNode.Scalar();

            if( text.size() > 2 && text.compare( text.size() - 2, 2, "pt" ) == 0 )
                text.resize( text.size() - 2 );

            while( !text.empty() && std::isspace( static_cast<unsigned char>( text.back() ) ) )
                text.pop_back();

            if( std::optional<double> v = sexpr::ToNumber( text ); v && *v > 0.0 && *v < 1000.0 )
                return *v;
        }

        fail( aNode, "'" + aPath + "' must be a width in points greater than 0, like 0.5 or 0.5pt" );
    }

    std::vector<std::string> stringList( const YAML::Node& aNode, const std::string& aPath ) const
    {
        if( !aNode.IsSequence() )
            fail( aNode, "'" + aPath + "' must be a list, like [Edge.Cuts, F.Cu]" );

        std::vector<std::string> result;

        for( const YAML::Node& item : aNode )
        {
            if( !item.IsScalar() || item.Scalar().empty() )
                fail( item, "'" + aPath + "' must only contain layer names" );

            result.push_back( item.Scalar() );
        }

        return result;
    }

    void stroke( const YAML::Node& aNode, const std::string& aPath, Stroke& aStroke ) const
    {
        forEachKey( aNode, aPath,
                    { { "width", [&]( const YAML::Node& n ) { aStroke.widthPt = width( n, aPath + ".width" ); } },
                      { "color", [&]( const YAML::Node& n ) { aStroke.color = color( n, aPath + ".color" ); } } } );
    }

    static std::optional<Color> parseColor( const std::string& aText )
    {
        if( aText.empty() || aText[0] != '#' || ( aText.size() != 4 && aText.size() != 7 ) )
            return std::nullopt;

        auto hexDigit = []( char c ) -> int
        {
            if( c >= '0' && c <= '9' )
                return c - '0';

            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

            if( c >= 'a' && c <= 'f' )
                return c - 'a' + 10;

            return -1;
        };

        std::array<int, 3> rgb{};
        const bool         isShort = aText.size() == 4;

        for( int i = 0; i < 3; ++i )
        {
            int hi = hexDigit( aText[isShort ? 1 + i : 1 + 2 * i] );
            int lo = isShort ? hi : hexDigit( aText[2 + 2 * i] );

            if( hi < 0 || lo < 0 )
                return std::nullopt;

            rgb[i] = hi * 16 + lo;
        }

        return Color{ static_cast<uint8_t>( rgb[0] ), static_cast<uint8_t>( rgb[1] ), static_cast<uint8_t>( rgb[2] ) };
    }

private:
    std::string m_source;
};

} // namespace


std::string Color::hex() const
{
    static const char digits[] = "0123456789abcdef";
    std::string       s = "#";

    for( uint8_t v : { r, g, b } )
    {
        s += digits[v >> 4];
        s += digits[v & 15];
    }

    return s;
}


const char* CategoryName( LineCategory aCategory )
{
    return CATEGORY_NAMES[static_cast<size_t>( aCategory )];
}


void ApplyStyle( const std::string& aText, const std::string& aSourceName, Style& aStyle )
{
    Reader     reader( aSourceName );
    YAML::Node root;

    try
    {
        root = YAML::Load( aText );
    }
    catch( const YAML::ParserException& e )
    {
        reader.fail( e.mark, e.msg );
    }

    // Work on a copy so a failed file leaves aStyle unchanged.
    Style style = aStyle;

    try
    {
        reader.forEachKey(
                root, "",
                {
                    { "background",
                      [&]( const YAML::Node& n )
                      {
                          if( n.IsScalar() && n.Scalar() == "transparent" )
                              style.background.reset();
                          else
                              style.background = reader.color( n, "background" );
                      } },
                    { "visible",
                      [&]( const YAML::Node& n )
                      {
                          Visibility& v = style.visible;
                          auto        flag = [&]( bool& aFlag, const char* aName )
                          {
                              return [&aFlag, aName, &reader]( const YAML::Node& f )
                              { aFlag = reader.boolean( f, std::string( "visible." ) + aName ); };
                          };

                          reader.forEachKey(
                                  n, "visible",
                                  { { "layers", [&]( const YAML::Node& l ) { v.layers = reader.stringList( l, "visible.layers" ); } },
                                    { "models", flag( v.models, "models" ) },
                                    { "dnp", flag( v.dnp, "dnp" ) },
                                    { "pads", flag( v.pads, "pads" ) },
                                    { "tracks", flag( v.tracks, "tracks" ) },
                                    { "vias", flag( v.vias, "vias" ) },
                                    { "graphics", flag( v.graphics, "graphics" ) } } );
                      } },
                    { "lines",
                      [&]( const YAML::Node& n )
                      {
                          std::map<std::string, std::function<void( const YAML::Node& )>> handlers;

                          for( size_t i = 0; i < LINE_CATEGORY_COUNT; ++i )
                          {
                              const std::string name = CATEGORY_NAMES[i];
                              const std::string path = "lines." + name;
                              CategoryStrokes&  strokes = style.lines[i];
                              const bool        meshCategory = ( i == static_cast<size_t>( LineCategory::Board )
                                                          || i == static_cast<size_t>( LineCategory::Models ) );

                              if( meshCategory )
                              {
                                  handlers[name] = [&reader, &strokes, path]( const YAML::Node& c )
                                  {
                                      reader.forEachKey(
                                              c, path,
                                              { { "outline", [&]( const YAML::Node& s ) { reader.stroke( s, path + ".outline", strokes.outline ); } },
                                                { "edge", [&]( const YAML::Node& s ) { reader.stroke( s, path + ".edge", strokes.edge ); } } } );
                                  };
                              }
                              else
                              {
                                  handlers[name] = [&reader, &strokes, path]( const YAML::Node& c )
                                  { reader.stroke( c, path, strokes.edge ); };
                              }
                          }

                          reader.forEachKey( n, "lines", handlers );
                      } },
                } );
    }
    catch( const YAML::Exception& e )
    {
        reader.fail( e.mark, e.msg );
    }

    aStyle = std::move( style );
}


const Style& DefaultStyle()
{
    static const Style style = []
    {
        Style s;
        ApplyStyle( DefaultStyleText(), "built-in default style", s );
        return s;
    }();

    return style;
}


Style LoadStyleFile( const std::filesystem::path& aFile )
{
    std::ifstream in( aFile, std::ios::binary );

    if( !in )
        throw StyleError( aFile.string() + ": cannot read style file" );

    std::stringstream text;
    text << in.rdbuf();

    Style style = DefaultStyle();
    ApplyStyle( text.str(), aFile.string(), style );
    return style;
}

} // namespace kirend::style
