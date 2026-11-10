#include "models/model_loader.h"

#include "paths/embedded_files.h"

#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <VrmlData_Appearance.hxx>
#include <NCollection_DataMap.hxx>
#include <VrmlData_Scene.hxx>

#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace kirend::models {

namespace {

/// KiCad VRML units are 0.1 inch.
constexpr double VRML_UNIT_TO_MM = 2.54;

void silenceOcct()
{
    static std::once_flag once;
    std::call_once( once, []
    {
        // OCCT prints progress and warnings to stdout by default; keep the CLI output clean.
        Message::DefaultMessenger()->RemovePrinters( STANDARD_TYPE( Message_Printer ) );
    } );
}


TopoDS_Shape readStep( std::istream& aStream, const std::string& aName )
{
    STEPControl_Reader reader;

    if( reader.ReadStream( aName.c_str(), aStream ) != IFSelect_RetDone )
        throw std::runtime_error( "not a readable STEP file" );

    if( reader.TransferRoots() == 0 )
        throw std::runtime_error( "STEP file contains no shapes" );

    TopoDS_Shape shape = reader.OneShape();

    if( shape.IsNull() )
        throw std::runtime_error( "STEP file contains no shapes" );

    return shape;
}


TopoDS_Shape readVrml( std::istream& aStream, const std::filesystem::path& aDir )
{
    VrmlData_Scene scene;
    scene.SetLinearScale( VRML_UNIT_TO_MM );

    if( !aDir.empty() )
        scene.SetVrmlDir( TCollection_ExtendedString( aDir.string().c_str(), true ) );

    scene << aStream;

    if( scene.Status() != VrmlData_StatusOK )
        throw std::runtime_error( "not a readable VRML file (error " + std::to_string( scene.Status() ) + ")" );

    NCollection_DataMap<Handle( TopoDS_TShape ), Handle( VrmlData_Appearance )> appearances;
    TopoDS_Shape                      shape = scene.GetShape( appearances );

    if( shape.IsNull() )
        throw std::runtime_error( "VRML file contains no shapes" );

    return shape;
}


/// Convert a shape with triangulated faces to a mesh. Sharp B-rep edges become feature edges.
render::Mesh shapeToMesh( const TopoDS_Shape& aShape, bool aUseBRepEdges )
{
    render::Mesh mesh;

    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgeFaces;

    if( aUseBRepEdges )
        TopExp::MapShapesAndAncestors( aShape, TopAbs_EDGE, TopAbs_FACE, edgeFaces );

    for( TopExp_Explorer faceExp( aShape, TopAbs_FACE ); faceExp.More(); faceExp.Next() )
    {
        const TopoDS_Face&           face = TopoDS::Face( faceExp.Current() );
        TopLoc_Location              loc;
        Handle( Poly_Triangulation ) tri = BRep_Tool::Triangulation( face, loc );

        if( tri.IsNull() || tri->NbTriangles() == 0 )
            continue;

        const gp_Trsf  trsf = loc.Transformation();
        const uint32_t base = static_cast<uint32_t>( mesh.vertices.size() );

        for( int i = 1; i <= tri->NbNodes(); ++i )
        {
            const gp_Pnt p = tri->Node( i ).Transformed( trsf );
            mesh.vertices.emplace_back( p.X(), p.Y(), p.Z() );
        }

        for( int t = 1; t <= tri->NbTriangles(); ++t )
        {
            int a, b, c;
            tri->Triangle( t ).Get( a, b, c );
            mesh.triangles.push_back( { base + a - 1, base + b - 1, base + c - 1 } );
        }

        if( !aUseBRepEdges )
            continue;

        for( TopExp_Explorer edgeExp( face, TopAbs_EDGE ); edgeExp.More(); edgeExp.Next() )
        {
            const TopoDS_Edge& edge = TopoDS::Edge( edgeExp.Current() );

            if( BRep_Tool::Degenerated( edge ) )
                continue;

            // Seam of a closed surface (cylinder side): never a real edge.
            if( BRep_Tool::IsClosed( edge, face ) )
                continue;

            const int index = edgeFaces.FindIndex( edge );

            if( index > 0 )
            {
                const NCollection_List<TopoDS_Shape>& faces = edgeFaces( index );

                if( faces.Extent() == 2 )
                {
                    const TopoDS_Face& f1 = TopoDS::Face( faces.First() );
                    const TopoDS_Face& f2 = TopoDS::Face( faces.Last() );

                    if( BRep_Tool::Continuity( edge, f1, f2 ) >= GeomAbs_G1 )
                        continue; // smooth transition: only drawn where it is a silhouette
                }
            }

            Handle( Poly_PolygonOnTriangulation ) poly = BRep_Tool::PolygonOnTriangulation( edge, tri, loc );

            if( poly.IsNull() )
                continue;

            const int n = poly->NbNodes();

            for( int i = 1; i < n; ++i )
            {
                mesh.featureEdges.push_back( { base + static_cast<uint32_t>( poly->Node( i ) - 1 ),
                                               base + static_cast<uint32_t>( poly->Node( i + 1 ) - 1 ) } );
            }
        }
    }

    return mesh;
}


std::string cacheKey( const ResolvedModel& aModel )
{
    if( aModel.kind == ResolvedModel::Kind::Embedded )
        return "embed:" + aModel.embedded->name + ":" + std::to_string( reinterpret_cast<uintptr_t>( aModel.embedded ) );

    return "file:" + aModel.path.string();
}

} // namespace


render::Mesh LoadModel( const ResolvedModel& aModel, const MeshQuality& aQuality )
{
    silenceOcct();

    const bool isStep = aModel.extension == ".step" || aModel.extension == ".stp";
    const bool isVrml = aModel.extension == ".wrl";

    if( !isStep && !isVrml )
        throw std::runtime_error( "unsupported model format '" + aModel.extension + "'" );

    std::string           data;
    std::filesystem::path dir;

    if( aModel.kind == ResolvedModel::Kind::Embedded )
    {
        std::vector<uint8_t> bytes = DecodeEmbeddedFile( *aModel.embedded );
        data.assign( bytes.begin(), bytes.end() );
    }
    else
    {
        std::ifstream in( aModel.path, std::ios::binary );

        if( !in )
            throw std::runtime_error( "cannot open file" );

        std::ostringstream buffer;
        buffer << in.rdbuf();
        data = buffer.str();
        dir = aModel.path.parent_path();
    }

    std::istringstream stream( data );

    if( isStep )
    {
        TopoDS_Shape shape = readStep( stream, aModel.displayName );
        BRepLib::EncodeRegularity( shape, aQuality.smoothAngleRad );
        BRepMesh_IncrementalMesh meshing( shape, aQuality.linearDeflectionMm, false,
                                          aQuality.angularDeflectionRad, true );
        return shapeToMesh( shape, true );
    }

    TopoDS_Shape shape = readVrml( stream, dir );
    return shapeToMesh( shape, false );
}


std::shared_ptr<const render::Mesh> ModelCache::get( const ResolvedModel& aModel, std::string& aError )
{
    const std::string key = cacheKey( aModel );

    if( auto it = m_cache.find( key ); it != m_cache.end() )
        return it->second;

    if( auto it = m_errors.find( key ); it != m_errors.end() )
    {
        aError = it->second;
        return nullptr;
    }

    try
    {
        auto mesh = std::make_shared<const render::Mesh>( LoadModel( aModel, m_quality ) );
        m_cache[key] = mesh;
        return mesh;
    }
    catch( const std::exception& e )
    {
        m_errors[key] = e.what();
        aError = e.what();
        return nullptr;
    }
}

} // namespace kirend::models
