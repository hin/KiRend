#include "paths/path_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace kirend {

namespace {

/// Major version used for the "current" versioned variable names (KICAD10_3DMODEL_DIR).
constexpr int KICAD_MAJOR_VERSION = 10;

std::optional<std::string> getEnv( const std::string& aName )
{
    if( const char* value = std::getenv( aName.c_str() ) )
        return std::string( value );

    return std::nullopt;
}


std::string toLower( std::string aText )
{
    std::transform( aText.begin(), aText.end(), aText.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return aText;
}


/// Matches KICADn_<SUFFIX> (KiCad's ENV_VAR::IsVersionedEnvVar).
bool isVersionedVar( const std::string& aName, const std::string& aSuffix )
{
    static const std::regex pattern( "^KICAD[0-9]+_(.+)$" );
    std::smatch             m;
    return std::regex_match( aName, m, pattern ) && m[1] == aSuffix;
}


bool fileExists( const fs::path& aPath )
{
    std::error_code ec;
    return fs::is_regular_file( aPath, ec );
}

} // namespace


fs::path DefaultKiCadConfigBase()
{
#if defined( _WIN32 )
    if( std::optional<std::string> appdata = getEnv( "APPDATA" ) )
        return fs::path( *appdata ) / "kicad";

    return {};
#elif defined( __APPLE__ )
    if( std::optional<std::string> home = getEnv( "HOME" ) )
        return fs::path( *home ) / "Library" / "Preferences" / "kicad";

    return {};
#else
    if( std::optional<std::string> xdg = getEnv( "XDG_CONFIG_HOME" ); xdg && !xdg->empty() )
        return fs::path( *xdg ) / "kicad";

    if( std::optional<std::string> home = getEnv( "HOME" ) )
        return fs::path( *home ) / ".config" / "kicad";

    return {};
#endif
}


std::optional<fs::path> FindKiCadConfigDir( const fs::path& aBase )
{
    std::error_code ec;

    if( aBase.empty() || !fs::is_directory( aBase, ec ) )
        return std::nullopt;

    std::optional<fs::path> best;
    std::pair<int, int>     bestVersion{ -1, -1 };

    for( const fs::directory_entry& entry : fs::directory_iterator( aBase, ec ) )
    {
        if( !entry.is_directory() )
            continue;

        int major = 0;
        int minor = 0;

        if( std::sscanf( entry.path().filename().string().c_str(), "%d.%d", &major, &minor ) != 2 )
            continue;

        if( major < KICAD_MAJOR_VERSION )
            continue;

        if( std::pair{ major, minor } > bestVersion && fileExists( entry.path() / "kicad_common.json" ) )
        {
            bestVersion = { major, minor };
            best = entry.path();
        }
    }

    return best;
}


std::string DefaultStock3dModelsPath()
{
    if( std::optional<std::string> stock = getEnv( "KICAD_STOCK_DATA_HOME" ); stock && !stock->empty() )
        return ( fs::path( *stock ) / "3dmodels" ).string();

#if defined( _WIN32 )
    return "C:\\Program Files\\KiCad\\" + std::to_string( KICAD_MAJOR_VERSION ) + ".0\\share\\kicad\\3dmodels";
#elif defined( __APPLE__ )
    return "/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels";
#else
    for( const char* candidate : { "/usr/share/kicad/3dmodels", "/usr/local/share/kicad/3dmodels" } )
    {
        std::error_code ec;

        if( fs::is_directory( candidate, ec ) )
            return candidate;
    }

    return "/usr/share/kicad/3dmodels";
#endif
}


PathResolver::PathResolver( const ResolverSettings& aSettings ) : m_settings( aSettings )
{
    std::error_code ec;
    fs::path        board = fs::absolute( aSettings.boardFile, ec );
    m_projectDir = board.parent_path();

    loadProjectFile();

    fs::path configDir = aSettings.kicadConfigDir;

    if( configDir.empty() )
    {
        if( std::optional<std::string> home = getEnv( "KICAD_CONFIG_HOME" ); home && !home->empty() )
        {
            // KICAD_CONFIG_HOME replaces the base directory; the version is appended.
            if( std::optional<fs::path> found = FindKiCadConfigDir( *home ) )
                configDir = *found;
        }
        else if( std::optional<fs::path> found = FindKiCadConfigDir( DefaultKiCadConfigBase() ) )
        {
            configDir = *found;
        }
    }

    if( !configDir.empty() )
        loadKiCadCommon( configDir );

    m_stock3dModels = DefaultStock3dModelsPath();
}


void PathResolver::loadProjectFile()
{
    fs::path pro = m_settings.boardFile;
    pro.replace_extension( ".kicad_pro" );

    std::ifstream in( pro );

    if( !in )
        return;

    try
    {
        nlohmann::json json = nlohmann::json::parse( in, nullptr, true, true );

        if( json.contains( "text_variables" ) && json["text_variables"].is_object() )
        {
            for( auto& [key, value] : json["text_variables"].items() )
            {
                if( value.is_string() )
                    m_projectVars[key] = value.get<std::string>();
            }
        }
    }
    catch( const std::exception& )
    {
        // A broken project file only means no project variables.
    }
}


void PathResolver::loadKiCadCommon( const fs::path& aConfigDir )
{
    fs::path      file = aConfigDir / "kicad_common.json";
    std::ifstream in( file );

    if( !in )
        return;

    try
    {
        nlohmann::json json = nlohmann::json::parse( in, nullptr, true, true );
        m_kicadCommonPath = file;

        const nlohmann::json* vars = nullptr;

        if( json.contains( "environment" ) && json["environment"].contains( "vars" ) )
            vars = &json["environment"]["vars"];

        if( vars && vars->is_object() )
        {
            for( auto& [key, value] : vars->items() )
            {
                if( value.is_string() )
                    m_commonVars[key] = value.get<std::string>();
            }
        }

        if( json.contains( "system" ) && json["system"].contains( "extra_3d_search_dirs" )
            && json["system"]["extra_3d_search_dirs"].is_array() )
        {
            for( const nlohmann::json& dir : json["system"]["extra_3d_search_dirs"] )
            {
                if( dir.is_string() )
                    m_extraSearchDirs.push_back( dir.get<std::string>() );
            }
        }
    }
    catch( const std::exception& )
    {
    }
}


std::optional<std::string> PathResolver::lookupVariable( const std::string& aName ) const
{
    // 1. -D overrides and project text variables (PROJECT::TextVarResolver).
    if( auto it = m_settings.defines.find( aName ); it != m_settings.defines.end() )
        return it->second;

    if( auto it = m_projectVars.find( aName ); it != m_projectVars.end() )
        return it->second;

    if( aName == "KIPRJMOD" )
        return m_projectDir.string();

    // 2. Process environment. KiCad copies kicad_common.json variables into its environment at
    //    start-up, unless the variable is already defined externally, so the real environment wins.
    if( std::optional<std::string> env = getEnv( aName ) )
        return env;

    if( auto it = m_commonVars.find( aName ); it != m_commonVars.end() )
        return it->second;

    // 3. Built-in default for the current 3D model directory.
    const std::string current3d = "KICAD" + std::to_string( KICAD_MAJOR_VERSION ) + "_3DMODEL_DIR";

    if( aName == current3d )
        return m_stock3dModels;

    // 4. Older (or newer) versioned names fall back to the current version's value.
    if( aName.find( "KISYS3DMOD" ) != std::string::npos || isVersionedVar( aName, "3DMODEL_DIR" ) )
        return lookupVariable( current3d );

    return std::nullopt;
}


std::string PathResolver::expandVariables( const std::string& aText ) const
{
    std::string result;
    result.reserve( aText.size() );

    for( size_t n = 0; n < aText.size(); ++n )
    {
        const char c = aText[n];

#if defined( _WIN32 )
        const bool windowsStyle = ( c == '%' );
#else
        const bool windowsStyle = false;
#endif

        if( c != '$' && !windowsStyle )
        {
            result += c;
            continue;
        }

        char close = 0;
        size_t start = n + 1;

        if( windowsStyle )
            close = '%';
        else if( start < aText.size() && aText[start] == '{' )
            close = '}', ++start;
        else if( start < aText.size() && aText[start] == '(' )
            close = ')', ++start;

        size_t m = start;

        while( m < aText.size()
               && ( std::isalnum( static_cast<unsigned char>( aText[m] ) ) || aText[m] == '_'
                    || aText[m] == ':' ) )
        {
            ++m;
        }

        const std::string name = aText.substr( start, m - start );
        const bool        closed = close == 0 || ( m < aText.size() && aText[m] == close );

        if( name.empty() || !closed )
        {
            result += c;
            continue;
        }

        if( std::optional<std::string> value = lookupVariable( name ) )
        {
            result += *value;
            n = ( close ? m : m - 1 );
        }
        else
        {
            // Unknown variable: keep the text unchanged.
            result.append( aText, n, ( close ? m + 1 : m ) - n );
            n = ( close ? m : m - 1 );
        }
    }

    return result;
}


std::optional<fs::path> PathResolver::findFile( const std::string& aExpanded ) const
{
    std::string name = aExpanded;

#if !defined( _WIN32 )
    std::replace( name.begin(), name.end(), '\\', '/' );
#endif

    fs::path p( name );

    // Absolute paths, expanded variables and paths relative to the working directory.
    if( fileExists( p ) )
        return fs::absolute( p ).lexically_normal();

    if( p.is_absolute() || name.rfind( ':', 0 ) == 0 )
        return std::nullopt;

    // Relative to the project directory.
    if( fs::path candidate = m_projectDir / p; fileExists( candidate ) )
        return candidate.lexically_normal();

    // Relative to the current 3D model directory (legacy behaviour).
    const std::string current3d = "KICAD" + std::to_string( KICAD_MAJOR_VERSION ) + "_3DMODEL_DIR";

    if( std::optional<std::string> base = lookupVariable( current3d ) )
    {
        if( fs::path candidate = fs::path( *base ) / p; fileExists( candidate ) )
            return candidate.lexically_normal();
    }

    return std::nullopt;
}


std::optional<fs::path> PathResolver::preferStep( const fs::path& aWrl ) const
{
    for( const char* ext : { ".step", ".stp", ".STEP", ".STP" } )
    {
        fs::path candidate = aWrl;
        candidate.replace_extension( ext );

        if( fileExists( candidate ) )
            return candidate;
    }

    return std::nullopt;
}


namespace {

/// Lower case, without a 3D extension, '-' and ' ' replaced by '_' (MODEL_SUBSTITUTION::normalizeStem).
std::string normalizeStem( const std::string& aFileName )
{
    std::string stem = aFileName;
    const std::string lower = toLower( stem );

    for( const char* ext : { ".step.gz", ".stp.gz", ".wrl", ".wrz", ".step", ".stp", ".stpz", ".iges", ".igs" } )
    {
        const std::string e( ext );

        if( lower.size() > e.size() && lower.compare( lower.size() - e.size(), e.size(), e ) == 0 )
        {
            stem = stem.substr( 0, stem.size() - e.size() );
            break;
        }
    }

    stem = toLower( stem );
    std::replace( stem.begin(), stem.end(), '-', '_' );
    std::replace( stem.begin(), stem.end(), ' ', '_' );
    return stem;
}

} // namespace


void PathResolver::buildStepCatalog() const
{
    m_catalogBuilt = true;

    // Same directories as MODEL_SUBSTITUTION::gatherScanDirs(): the resolver's search paths
    // (project directory and KiCad path variables, except footprint libraries and URLs), the
    // project's 3dshapes folder and the extra search directories from kicad_common.json.
    std::vector<fs::path> dirs;
    dirs.push_back( m_projectDir );

    std::map<std::string, std::string> vars = m_commonVars;
    const std::string version = std::to_string( KICAD_MAJOR_VERSION );

    for( const std::string& name : { "KICAD" + version + "_3DMODEL_DIR", "KICAD" + version + "_3RD_PARTY" } )
        vars.try_emplace( name, "" );

    for( const auto& [name, unused] : vars )
    {
        if( name == "KICAD_PTEMPLATES" || isVersionedVar( name, "FOOTPRINT_DIR" ) )
            continue;

        std::optional<std::string> value = lookupVariable( name );

        if( value && !value->empty() && value->find( "://" ) == std::string::npos )
            dirs.push_back( fs::path( expandVariables( *value ) ) );
    }

    dirs.push_back( m_projectDir / "3dshapes" );

    for( const std::string& dir : m_extraSearchDirs )
        dirs.push_back( fs::path( expandVariables( dir ) ) );

    std::set<std::string> seen;

    for( const fs::path& dir : dirs )
    {
        std::error_code ec;
        fs::path        norm = fs::weakly_canonical( dir, ec );

        if( ec || !fs::is_directory( norm, ec ) || !seen.insert( toLower( norm.string() ) ).second )
            continue;

        std::vector<fs::path> files;
        auto options = fs::directory_options::skip_permission_denied;

        for( auto it = fs::recursive_directory_iterator( norm, options, ec ); !ec && it != fs::recursive_directory_iterator();
             it.increment( ec ) )
        {
            if( !it->is_regular_file( ec ) )
                continue;

            const std::string ext = toLower( it->path().extension().string() );

            // Only the formats KiRend can read (KiCad also accepts .stpz, .gz and IGES).
            if( ext == ".step" || ext == ".stp" )
                files.push_back( it->path() );
        }

        std::sort( files.begin(), files.end() );

        for( const fs::path& file : files )
            m_stepCatalog[normalizeStem( file.filename().string() )].push_back( file );
    }
}


std::optional<fs::path> PathResolver::substituteMissingWrl( const std::string& aWrl ) const
{
    if( !m_catalogBuilt )
        buildStepCatalog();

    std::string name = aWrl;
    std::replace( name.begin(), name.end(), '\\', '/' );
    fs::path wrl( name );

    auto it = m_stepCatalog.find( normalizeStem( wrl.filename().string() ) );

    if( it == m_stepCatalog.end() || it->second.empty() )
        return std::nullopt;

    // Prefer a match in a folder with the same name (e.g. Resistor_SMD.3dshapes).
    const std::string parent = toLower( wrl.parent_path().filename().string() );

    if( !parent.empty() )
    {
        for( const fs::path& candidate : it->second )
        {
            if( toLower( candidate.parent_path().filename().string() ) == parent )
                return candidate;
        }
    }

    return it->second.front();
}


ResolvedModel PathResolver::resolveModel( const std::string& aModelPath, const pcb::Footprint& aFootprint,
                                          const pcb::Board& aBoard ) const
{
    ResolvedModel result;
    result.displayName = aModelPath;

    std::string expanded = expandVariables( aModelPath );

    auto extensionOf = []( const std::string& aName ) { return toLower( fs::path( aName ).extension().string() ); };

    const std::string embedPrefix = "kicad-embed://";

    if( expanded.rfind( embedPrefix, 0 ) == 0 )
    {
        const std::string name = expanded.substr( embedPrefix.size() );

        // Footprint files first, then board files (same order as KiCad).
        auto find = [&]( const std::string& aName ) -> const pcb::EmbeddedFile*
        {
            for( const auto* list : { &aFootprint.embeddedFiles, &aBoard.embeddedFiles } )
            {
                for( const pcb::EmbeddedFile& f : *list )
                {
                    if( f.name == aName && !f.data.empty() )
                        return &f;
                }
            }

            return nullptr;
        };

        const pcb::EmbeddedFile* file = find( name );

        if( !file )
            return result;

        result.kind = ResolvedModel::Kind::Embedded;
        result.embedded = file;
        result.extension = extensionOf( name );

        if( result.extension == ".wrl" )
        {
            const std::string stem = fs::path( name ).replace_extension().string();

            for( const char* ext : { ".step", ".stp", ".STEP", ".STP" } )
            {
                if( const pcb::EmbeddedFile* step = find( stem + ext ) )
                {
                    result.embedded = step;
                    result.extension = toLower( ext );
                    break;
                }
            }
        }

        return result;
    }

    std::optional<fs::path> path = findFile( expanded );

    if( !path )
    {
        const std::string ext = extensionOf( expanded );

        if( ext == ".wrl" || ext == ".wrz" )
            path = substituteMissingWrl( expanded );

        if( !path )
            return result;
    }

    result.kind = ResolvedModel::Kind::File;
    result.path = *path;
    result.extension = extensionOf( path->string() );

    if( result.extension == ".wrl" || result.extension == ".wrz" )
    {
        if( std::optional<fs::path> step = preferStep( *path ) )
        {
            result.path = *step;
            result.extension = extensionOf( step->string() );
        }
    }

    return result;
}

} // namespace kirend
