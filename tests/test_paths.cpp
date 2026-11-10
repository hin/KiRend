#include "paths/base64.h"
#include "paths/embedded_files.h"
#include "paths/path_resolver.h"
#include "pcb/board.h"

#include <gtest/gtest.h>
#include <zstd.h>

#include <cstdlib>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using namespace kirend;

namespace {

class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ( "kirend_test_" + std::to_string( rd() ) );
        fs::create_directories( m_path );
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all( m_path, ec );
    }

    const fs::path& path() const { return m_path; }

    fs::path touch( const std::string& aRelative ) const
    {
        fs::path p = m_path / aRelative;
        fs::create_directories( p.parent_path() );
        std::ofstream( p ) << "x";
        return p;
    }

private:
    fs::path m_path;
};


std::string encodeBase64( const std::vector<uint8_t>& aData )
{
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string        out;
    size_t             i = 0;

    for( ; i + 2 < aData.size(); i += 3 )
    {
        uint32_t v = ( aData[i] << 16 ) | ( aData[i + 1] << 8 ) | aData[i + 2];
        out += chars[( v >> 18 ) & 63];
        out += chars[( v >> 12 ) & 63];
        out += chars[( v >> 6 ) & 63];
        out += chars[v & 63];
    }

    if( i + 1 == aData.size() )
    {
        uint32_t v = aData[i] << 16;
        out += chars[( v >> 18 ) & 63];
        out += chars[( v >> 12 ) & 63];
        out += "==";
    }
    else if( i + 2 == aData.size() )
    {
        uint32_t v = ( aData[i] << 16 ) | ( aData[i + 1] << 8 );
        out += chars[( v >> 18 ) & 63];
        out += chars[( v >> 12 ) & 63];
        out += chars[( v >> 6 ) & 63];
        out += '=';
    }

    return out;
}

} // namespace


TEST( Base64, DecodesWithWhitespace )
{
    std::optional<std::vector<uint8_t>> data = DecodeBase64( "SGVs\n\tbG8=" );
    ASSERT_TRUE( data );
    EXPECT_EQ( std::string( data->begin(), data->end() ), "Hello" );
    EXPECT_FALSE( DecodeBase64( "SGV*bG8=" ) );
}


TEST( EmbeddedFiles, DecodesZstdBase64 )
{
    const std::string    text = "ISO-10303-21; embedded model test data";
    std::vector<uint8_t> compressed( ZSTD_compressBound( text.size() ) );
    size_t size = ZSTD_compress( compressed.data(), compressed.size(), text.data(), text.size(), 3 );
    compressed.resize( size );

    pcb::EmbeddedFile file;
    file.name = "part.step";
    file.data = encodeBase64( compressed );

    std::vector<uint8_t> decoded = DecodeEmbeddedFile( file );
    EXPECT_EQ( std::string( decoded.begin(), decoded.end() ), text );
}


TEST( PathResolver, ExpandsVariablesWithKiCadPrecedence )
{
    TempDir          dir;
    ResolverSettings settings;
    settings.boardFile = dir.path() / "board.kicad_pcb";
    settings.defines["MYLIB"] = "/libs";
    settings.kicadConfigDir = dir.path() / "no-config";

    {
        std::ofstream pro( dir.path() / "board.kicad_pro" );
        pro << R"({ "text_variables": { "PROJVAR": "proj", "MYLIB": "overridden-by-D" } })";
    }

    PathResolver resolver( settings );

    EXPECT_EQ( resolver.expandVariables( "${MYLIB}/a.step" ), "/libs/a.step" );
    EXPECT_EQ( resolver.expandVariables( "$(PROJVAR)/b" ), "proj/b" );
    EXPECT_EQ( resolver.expandVariables( "${KIPRJMOD}" ), fs::absolute( dir.path() ).string() );
    EXPECT_EQ( resolver.expandVariables( "${UNKNOWN_VAR_XYZ}/c" ), "${UNKNOWN_VAR_XYZ}/c" );

    // Older versioned variables resolve to the current 3D model directory.
    EXPECT_EQ( resolver.expandVariables( "${KICAD8_3DMODEL_DIR}" ), resolver.expandVariables( "${KICAD10_3DMODEL_DIR}" ) );
}


TEST( PathResolver, ReadsKiCadCommonJson )
{
    TempDir dir;
    fs::create_directories( dir.path() / "cfg" );

    {
        std::ofstream json( dir.path() / "cfg" / "kicad_common.json" );
        json << R"({ "environment": { "vars": { "COMMON_ONLY_VAR_TEST": "/from/common" } } })";
    }

    ResolverSettings settings;
    settings.boardFile = dir.path() / "board.kicad_pcb";
    settings.kicadConfigDir = dir.path() / "cfg";

    PathResolver resolver( settings );
    EXPECT_EQ( resolver.expandVariables( "${COMMON_ONLY_VAR_TEST}/x" ), "/from/common/x" );
    EXPECT_EQ( resolver.kicadCommonPath(), dir.path() / "cfg" / "kicad_common.json" );
}


TEST( PathResolver, StepWinsOverWrl )
{
    TempDir dir;
    dir.touch( "models/part.wrl" );
    fs::path step = dir.touch( "models/part.step" );
    dir.touch( "models/only.wrl" );

    ResolverSettings settings;
    settings.boardFile = dir.path() / "board.kicad_pcb";
    settings.kicadConfigDir = dir.path() / "no-config";
    PathResolver resolver( settings );

    pcb::Footprint fp;
    pcb::Board     board;

    ResolvedModel a = resolver.resolveModel( "${KIPRJMOD}/models/part.wrl", fp, board );
    ASSERT_EQ( a.kind, ResolvedModel::Kind::File );
    EXPECT_EQ( a.path.filename(), "part.step" );
    EXPECT_EQ( a.extension, ".step" );

    ResolvedModel b = resolver.resolveModel( "models/only.wrl", fp, board );
    ASSERT_EQ( b.kind, ResolvedModel::Kind::File );
    EXPECT_EQ( b.extension, ".wrl" );

    ResolvedModel c = resolver.resolveModel( "${KIPRJMOD}/models/missing.step", fp, board );
    EXPECT_EQ( c.kind, ResolvedModel::Kind::NotFound );
}


TEST( PathResolver, MissingWrlIsReplacedByStepFromSearchPaths )
{
    TempDir dir;
    dir.touch( "lib3d/Resistor_SMD.3dshapes/R_0603_1608Metric.step" );
    dir.touch( "lib3d/Other.3dshapes/R_0603_1608Metric.step" );

    ResolverSettings settings;
    settings.boardFile = dir.path() / "proj" / "board.kicad_pcb";
    settings.kicadConfigDir = dir.path() / "no-config";
    settings.defines["KICAD10_3DMODEL_DIR"] = ( dir.path() / "lib3d" ).string();
    PathResolver resolver( settings );

    pcb::Footprint fp;
    pcb::Board     board;

    ResolvedModel r = resolver.resolveModel( "${KICAD8_3DMODEL_DIR}/Resistor_SMD.3dshapes/R_0603_1608Metric.wrl", fp, board );
    ASSERT_EQ( r.kind, ResolvedModel::Kind::File );
    EXPECT_EQ( r.path.parent_path().filename(), "Resistor_SMD.3dshapes" );
}


TEST( PathResolver, ResolvesEmbeddedModels )
{
    TempDir          dir;
    ResolverSettings settings;
    settings.boardFile = dir.path() / "board.kicad_pcb";
    settings.kicadConfigDir = dir.path() / "no-config";
    PathResolver resolver( settings );

    pcb::Footprint fp;
    pcb::Board     board;
    board.embeddedFiles.push_back( { "part.step", "model", "KLUv", "" } );
    board.embeddedFiles.push_back( { "part.wrl", "model", "KLUv", "" } );

    ResolvedModel r = resolver.resolveModel( "kicad-embed://part.wrl", fp, board );
    ASSERT_EQ( r.kind, ResolvedModel::Kind::Embedded );
    EXPECT_EQ( r.embedded->name, "part.step" ); // STEP wins here too

    EXPECT_EQ( resolver.resolveModel( "kicad-embed://nope.step", fp, board ).kind, ResolvedModel::Kind::NotFound );
}
