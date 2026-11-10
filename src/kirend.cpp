#include "kirend.h"

#include "geometry/board_geometry.h"
#include "geometry/layer_lines.h"
#include "geometry/outline.h"
#include "models/mesh_builder.h"
#include "models/model_loader.h"
#include "output/png_writer.h"
#include "output/svg_writer.h"
#include "paths/path_resolver.h"
#include "pcb/board_reader.h"
#include "render/line_renderer.h"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace fs = std::filesystem;

namespace kirend {

namespace {

class Timer
{
public:
    Timer( bool aEnabled, std::ostream& aLog ) : m_enabled( aEnabled ), m_log( aLog ) {}

    void step( const std::string& aWhat )
    {
        if( !m_enabled )
            return;

        auto   now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>( now - m_last ).count();
        m_log << "  " << aWhat << " (" << static_cast<int>( ms ) << " ms)\n";
        m_last = now;
    }

private:
    bool                                  m_enabled;
    std::ostream&                         m_log;
    std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
};


fs::path defaultOutputPath( const RenderJob& aJob )
{
    // Same as kicad-cli: <board name>-<side number>.png in the current directory.
    fs::path name = aJob.input.filename();
    name.replace_extension();
    std::string file = name.string() + "-" + std::to_string( static_cast<int>( aJob.camera.side ) )
                       + ( aJob.format == OutputFormat::Svg ? ".svg" : ".png" );
    return file;
}

} // namespace


int RunRenderJob( const RenderJob& aJob, std::ostream& aLog )
{
    Timer timer( aJob.verbose, aLog );

    pcb::Board board;

    try
    {
        board = pcb::ReadBoardFile( aJob.input );
    }
    catch( const std::exception& e )
    {
        aLog << "Error: " << aJob.input.string() << ": " << e.what() << "\n";
        return exit_code::ERR_INVALID_INPUT_FILE;
    }

    for( const std::string& w : board.warnings )
        aLog << "Warning: " << w << "\n";

    timer.step( "read board" );

    // Check the requested layers before doing any work.
    std::vector<std::string> layers;
    bool                     drawBody = false;

    for( const std::string& name : aJob.layers )
    {
        std::string canonical = CanonicalLayerName( name, board );

        if( canonical.empty() )
        {
            aLog << "Error: unknown layer '" << name << "'\n";
            return exit_code::ERR_ARGS;
        }

        if( canonical == "Edge.Cuts" )
            drawBody = true;
        else if( std::find( layers.begin(), layers.end(), canonical ) == layers.end() )
            layers.push_back( canonical );
    }

    BoardOutline outline = BuildBoardOutline( board );
    const Box2   bbox = BoardBoundingBox( board, outline.fromEdgeCuts );

    if( !outline.fromEdgeCuts )
    {
        aLog << "Warning: " << outline.warning << "\n";

        OutlinePiece piece;
        piece.outer = { bbox.min, { bbox.max.x, bbox.min.y }, bbox.max, { bbox.min.x, bbox.max.y } };
        outline.pieces.push_back( piece );
    }

    const BoardHeights heights = ComputeBoardHeights( board );
    const double       maxSize = std::max( bbox.size().x, bbox.size().y );

    render::Camera camera( aJob.camera, bbox.center(), maxSize );
    render::Scene  scene;

    std::vector<std::string> warnings;

    if( drawBody )
    {
        scene.meshes.push_back( BuildBoardBodyMesh( outline, CollectDrillHoles( board ), heights, warnings ) );
        timer.step( "board body" );
    }

    for( const std::string& layer : layers )
        scene.lines.push_back( BuildLayerLines( board, layer, heights ) );

    // 3D models
    ResolverSettings resolverSettings;
    resolverSettings.boardFile = aJob.input;
    resolverSettings.defines = aJob.defines;
    PathResolver resolver( resolverSettings );

    if( aJob.verbose && !resolver.kicadCommonPath().empty() )
        aLog << "Using KiCad settings " << resolver.kicadCommonPath().string() << "\n";

    models::MeshQuality quality;
    quality.linearDeflectionMm = std::clamp( camera.pixelSizeMm() * 0.25, 0.002, 0.2 );
    models::ModelCache cache( quality );

    int missing = 0;
    int loaded = 0;

    for( const pcb::Footprint& fp : board.footprints )
    {
        // KiCad's 3D viewer hides DNP footprints by default.
        if( fp.dnp )
            continue;

        for( const pcb::Model3D& model : fp.models )
        {
            if( model.hidden || model.path.empty() || model.opacity <= 0.0 )
                continue;

            ResolvedModel resolved = resolver.resolveModel( model.path, fp, board );

            if( resolved.kind == ResolvedModel::Kind::NotFound )
            {
                ++missing;
                aLog << "Warning: " << fp.reference << ": 3D model not found: " << model.path << "\n";
                continue;
            }

            if( aJob.verbose )
            {
                aLog << "  " << fp.reference << ": "
                     << ( resolved.kind == ResolvedModel::Kind::Embedded ? "embedded " + resolved.embedded->name
                                                                         : resolved.path.string() )
                     << "\n";
            }

            std::string error;
            std::shared_ptr<const render::Mesh> mesh = cache.get( resolved, error );

            if( !mesh )
            {
                ++missing;
                aLog << "Warning: " << fp.reference << ": cannot load 3D model " << model.path << ": "
                     << error << "\n";
                continue;
            }

            scene.meshes.push_back(
                    models::TransformMesh( *mesh, models::ModelTransform( fp, model, heights ) ) );
            ++loaded;
        }
    }

    timer.step( "loaded " + std::to_string( loaded ) + " models" );

    for( const std::string& w : warnings )
        aLog << "Warning: " << w << "\n";

    std::vector<render::Polyline2D> lines = render::RenderLines( scene, camera );
    timer.step( "hidden line removal (" + std::to_string( lines.size() ) + " polylines)" );

    output::SvgStyle style = output::DefaultStyle( camera.imageHeight() );
    style.background = aJob.background == BackgroundStyle::Transparent ? output::Background::Transparent
                                                                        : output::Background::White;

    const std::string svg = output::WriteSvg( lines, camera.imageWidth(), camera.imageHeight(), style );
    const fs::path    outPath = aJob.output.empty() ? defaultOutputPath( aJob ) : aJob.output;

    try
    {
        if( outPath.has_parent_path() )
            fs::create_directories( outPath.parent_path() );

        if( aJob.format == OutputFormat::Svg )
        {
            std::ofstream out( outPath, std::ios::binary );

            if( !out )
                throw std::runtime_error( "cannot write '" + outPath.string() + "'" );

            out << svg;
        }
        else
        {
            output::WritePng( svg, camera.imageWidth(), camera.imageHeight(), aJob.dpi, outPath );
        }
    }
    catch( const std::exception& e )
    {
        aLog << "Error: " << e.what() << "\n";
        return exit_code::ERR_INVALID_OUTPUT_CONFLICT;
    }

    timer.step( "wrote " + outPath.string() );

    if( missing > 0 )
        aLog << missing << " 3D model(s) were skipped\n";

    return exit_code::OK;
}

} // namespace kirend
