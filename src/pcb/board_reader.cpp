#include "pcb/board_reader.h"

#include "geometry/arc.h"
#include "sexpr/sexpr.h"

#include <fstream>
#include <sstream>

namespace kirend::pcb {

using sexpr::Node;

namespace {

Vec2 readXY( const Node& aNode )
{
    return { aNode.number( 1 ), aNode.number( 2 ) };
}


Vec2 readPoint( const Node& aParent, std::string_view aKeyword )
{
    const Node* node = aParent.find( aKeyword );

    if( !node )
    {
        throw sexpr::ParseError( "missing (" + std::string( aKeyword ) + ") in (" + aParent.keyword()
                                         + ")",
                                 aParent.line );
    }

    return readXY( *node );
}


Vec3 readXYZ( const Node& aParent, std::string_view aKeyword, Vec3 aDefault )
{
    const Node* node = aParent.find( aKeyword );

    if( !node )
        return aDefault;

    const Node* xyz = node->find( "xyz" );

    if( !xyz )
        return aDefault;

    return { xyz->number( 1 ), xyz->number( 2 ), xyz->number( 3 ) };
}


double readStrokeWidth( const Node& aNode )
{
    if( const Node* stroke = aNode.find( "stroke" ) )
    {
        if( std::optional<double> w = stroke->numberValue( "width" ) )
            return *w;
    }

    if( std::optional<double> w = aNode.numberValue( "width" ) )
        return *w;

    return 0.0;
}


bool readFill( const Node& aNode )
{
    const Node* fill = aNode.find( "fill" );

    if( !fill )
        return false;

    if( fill->children.size() >= 2 && fill->children[1].isAtom() )
    {
        const std::string& v = fill->children[1].text;
        return v == "yes" || v == "solid" || v == "true";
    }

    // (fill (type solid)) style
    if( std::optional<std::string> type = fill->value( "type" ) )
        return *type != "none" && *type != "no";

    return false;
}


/// Read a (pts (xy ..) (arc (start)(mid)(end)) ...) list into a polyline.
std::vector<Vec2> readPts( const Node& aPts )
{
    std::vector<Vec2> points;

    for( const Node& child : aPts.children )
    {
        if( !child.isList() )
            continue;

        if( child.keyword() == "xy" )
        {
            points.push_back( readXY( child ) );
        }
        else if( child.keyword() == "arc" )
        {
            std::vector<Vec2> arc = ApproximateArc( readPoint( child, "start" ), readPoint( child, "mid" ),
                                                    readPoint( child, "end" ) );

            if( !points.empty() && points.back() == arc.front() )
                points.pop_back();

            points.insert( points.end(), arc.begin(), arc.end() );
        }
    }

    return points;
}


/// Parse gr_* / fp_* graphic shapes. Returns false for non-shape items (text etc.).
bool readShape( const Node& aNode, Shape& aShape )
{
    std::string kw = aNode.keyword();

    if( kw.size() > 3 && ( kw.rfind( "gr_", 0 ) == 0 || kw.rfind( "fp_", 0 ) == 0 ) )
        kw = kw.substr( 3 );
    else
        return false;

    aShape.layer = aNode.value( "layer" ).value_or( "" );
    aShape.width = readStrokeWidth( aNode );
    aShape.filled = readFill( aNode );

    if( kw == "line" )
    {
        aShape.kind = Shape::Kind::Segment;
        aShape.start = readPoint( aNode, "start" );
        aShape.end = readPoint( aNode, "end" );
    }
    else if( kw == "arc" )
    {
        aShape.kind = Shape::Kind::Arc;
        aShape.start = readPoint( aNode, "start" );
        aShape.mid = readPoint( aNode, "mid" );
        aShape.end = readPoint( aNode, "end" );
    }
    else if( kw == "circle" )
    {
        aShape.kind = Shape::Kind::Circle;
        aShape.start = readPoint( aNode, "center" );
        aShape.end = readPoint( aNode, "end" );
    }
    else if( kw == "rect" )
    {
        aShape.kind = Shape::Kind::Rect;
        aShape.start = readPoint( aNode, "start" );
        aShape.end = readPoint( aNode, "end" );
        aShape.cornerRadius = aNode.numberValue( "radius" ).value_or( 0.0 );
    }
    else if( kw == "poly" )
    {
        aShape.kind = Shape::Kind::Poly;

        if( const Node* pts = aNode.find( "pts" ) )
            aShape.points = readPts( *pts );
    }
    else if( kw == "curve" )
    {
        aShape.kind = Shape::Kind::Bezier;
        const Node* pts = aNode.find( "pts" );

        if( !pts )
            return false;

        std::vector<const Node*> xy = pts->findAll( "xy" );

        if( xy.size() != 4 )
            return false;

        aShape.start = readXY( *xy[0] );
        aShape.ctrl1 = readXY( *xy[1] );
        aShape.ctrl2 = readXY( *xy[2] );
        aShape.end = readXY( *xy[3] );
    }
    else
    {
        return false;
    }

    return true;
}


/// Move a shape from footprint-local to board coordinates.
void transformShape( Shape& aShape, const Vec2& aOrigin, double aAngle )
{
    auto xf = [&]( const Vec2& p ) { return RotateKiCad( p, aAngle ) + aOrigin; };

    aShape.start = xf( aShape.start );
    aShape.mid = xf( aShape.mid );
    aShape.end = xf( aShape.end );
    aShape.ctrl1 = xf( aShape.ctrl1 );
    aShape.ctrl2 = xf( aShape.ctrl2 );

    for( Vec2& p : aShape.points )
        p = xf( p );

    if( aShape.kind == Shape::Kind::Rect && aAngle != 0.0 )
    {
        // A rotated rectangle can't be stored as start/end any more; turn it into a polygon.
        // (KiCad does the same for rotations that are not multiples of 90 degrees.)
        Vec2 a = RotateKiCad( aShape.start - aOrigin, -aAngle );
        Vec2 b = RotateKiCad( aShape.end - aOrigin, -aAngle );
        aShape.kind = Shape::Kind::Poly;
        aShape.points = { xf( a ), xf( { b.x, a.y } ), xf( b ), xf( { a.x, b.y } ) };
    }
}


std::vector<EmbeddedFile> readEmbeddedFiles( const Node& aNode )
{
    std::vector<EmbeddedFile> files;

    for( const Node* file : aNode.findAll( "file" ) )
    {
        EmbeddedFile ef;
        ef.name = file->value( "name" ).value_or( "" );
        ef.type = file->value( "type" ).value_or( "" );
        ef.data = file->value( "data" ).value_or( "" );
        ef.checksum = file->value( "checksum" ).value_or( "" );
        files.push_back( std::move( ef ) );
    }

    return files;
}


Pad readPad( const Node& aNode, const Footprint& aFootprint, int aCopperLayerCount )
{
    Pad pad;
    pad.number = aNode.atom( 1 );

    const std::string& type = aNode.atom( 2 );

    if( type == "thru_hole" )
        pad.type = Pad::Type::ThroughHole;
    else if( type == "smd" )
        pad.type = Pad::Type::Smd;
    else if( type == "connect" )
        pad.type = Pad::Type::Connect;
    else if( type == "np_thru_hole" )
        pad.type = Pad::Type::NpThroughHole;

    const std::string& shape = aNode.atom( 3 );

    if( shape == "circle" )
        pad.shape = Pad::ShapeKind::Circle;
    else if( shape == "rect" )
        pad.shape = Pad::ShapeKind::Rect;
    else if( shape == "oval" )
        pad.shape = Pad::ShapeKind::Oval;
    else if( shape == "trapezoid" )
        pad.shape = Pad::ShapeKind::Trapezoid;
    else if( shape == "roundrect" )
        pad.shape = Pad::ShapeKind::RoundRect;
    else if( shape == "custom" )
        pad.shape = Pad::ShapeKind::Custom;

    Vec2 local;

    if( const Node* at = aNode.find( "at" ) )
    {
        local = readXY( *at );

        // The stored angle is the absolute pad orientation.
        if( at->children.size() > 3 )
            pad.angle = at->number( 3 );
    }

    pad.position = aFootprint.position + RotateKiCad( local, aFootprint.angle );

    if( const Node* size = aNode.find( "size" ) )
        pad.size = readXY( *size );

    if( const Node* delta = aNode.find( "rect_delta" ) )
        pad.rectDelta = readXY( *delta );

    pad.roundRectRatio = aNode.numberValue( "roundrect_rratio" ).value_or( 0.0 );
    pad.chamferRatio = aNode.numberValue( "chamfer_ratio" ).value_or( 0.0 );

    if( const Node* chamfer = aNode.find( "chamfer" ) )
    {
        if( chamfer->hasFlag( "top_left" ) )
            pad.chamferCorners |= 1;
        if( chamfer->hasFlag( "top_right" ) )
            pad.chamferCorners |= 2;
        if( chamfer->hasFlag( "bottom_left" ) )
            pad.chamferCorners |= 4;
        if( chamfer->hasFlag( "bottom_right" ) )
            pad.chamferCorners |= 8;
    }

    if( const Node* drill = aNode.find( "drill" ) )
    {
        std::vector<double> numbers;

        for( size_t i = 1; i < drill->children.size(); ++i )
        {
            const Node& c = drill->children[i];

            if( c.isAtom() && c.text == "oval" )
                pad.drill.oval = true;
            else if( c.isAtom() )
                numbers.push_back( drill->number( i ) );
            else if( c.keyword() == "offset" )
                pad.drill.offset = readXY( c );
        }

        if( !numbers.empty() )
        {
            pad.drill.sizeX = numbers[0];
            pad.drill.sizeY = numbers.size() > 1 ? numbers[1] : numbers[0];
        }
    }

    if( const Node* layers = aNode.find( "layers" ) )
    {
        for( size_t i = 1; i < layers->children.size(); ++i )
        {
            if( !layers->children[i].isAtom() )
                continue;

            for( std::string& l : ExpandLayerWildcard( layers->children[i].text, aCopperLayerCount ) )
                pad.layers.push_back( std::move( l ) );
        }
    }

    if( const Node* options = aNode.find( "options" ) )
    {
        if( std::optional<std::string> anchor = options->value( "anchor" ) )
            pad.customAnchorCircle = ( *anchor == "circle" );
    }

    if( const Node* primitives = aNode.find( "primitives" ) )
    {
        for( const Node& child : primitives->children )
        {
            Shape prim;

            if( child.isList() && readShape( child, prim ) )
                pad.primitives.push_back( std::move( prim ) );
        }
    }

    return pad;
}


Footprint readFootprint( const Node& aNode, int aCopperLayerCount )
{
    Footprint fp;

    if( aNode.children.size() > 1 && aNode.children[1].isAtom() )
        fp.libId = aNode.children[1].text;

    fp.flipped = aNode.value( "layer" ).value_or( "F.Cu" ) == "B.Cu";

    if( const Node* at = aNode.find( "at" ) )
    {
        fp.position = readXY( *at );

        if( at->children.size() > 3 )
            fp.angle = at->number( 3 );
    }

    if( const Node* attr = aNode.find( "attr" ) )
    {
        fp.smd = attr->hasFlag( "smd" );
        fp.throughHole = attr->hasFlag( "through_hole" );
        fp.boardOnly = attr->hasFlag( "board_only" );
        fp.excludeFromPos = attr->hasFlag( "exclude_from_pos_files" );
        fp.dnp = attr->hasFlag( "dnp" );
    }

    for( const Node& child : aNode.children )
    {
        if( !child.isList() )
            continue;

        const std::string& kw = child.keyword();

        if( kw == "property" && child.children.size() > 2 && child.atom( 1 ) == "Reference" )
        {
            fp.reference = child.atom( 2 );
        }
        else if( kw == "pad" )
        {
            fp.pads.push_back( readPad( child, fp, aCopperLayerCount ) );
        }
        else if( kw == "model" )
        {
            Model3D model;
            model.path = child.atom( 1 );
            model.offset = readXYZ( child, "offset", {} );
            model.scale = readXYZ( child, "scale", { 1, 1, 1 } );
            model.rotation = readXYZ( child, "rotate", {} );
            model.hidden = child.hasFlag( "hide" ) || child.boolValue( "hide" ).value_or( false );
            model.opacity = child.numberValue( "opacity" ).value_or( 1.0 );
            fp.models.push_back( std::move( model ) );
        }
        else if( kw == "embedded_files" )
        {
            fp.embeddedFiles = readEmbeddedFiles( child );
        }
        else
        {
            Shape shape;

            if( readShape( child, shape ) )
            {
                transformShape( shape, fp.position, fp.angle );
                fp.graphics.push_back( std::move( shape ) );
            }
        }
    }

    return fp;
}


void readStackup( const Node& aStackup, StackupInfo& aInfo )
{
    aInfo.present = true;
    aInfo.dielectricThickness = 0.0;

    // Minimal thickness KiCad uses for zero-thickness copper and mask layers (1 um).
    constexpr double MIN_THICKNESS = 0.001;

    for( const Node* layer : aStackup.findAll( "layer" ) )
    {
        const std::string name = layer->atom( 1 );
        const std::string type = layer->value( "type" ).value_or( "" );

        double thickness = 0.0;

        for( const Node* t : layer->findAll( "thickness" ) )
            thickness += t->number( 1 );

        if( type == "core" || type == "prepreg" )
        {
            aInfo.dielectricThickness += thickness;
        }
        else if( type == "copper" )
        {
            double copper = std::max( thickness, MIN_THICKNESS );

            if( name == "F.Cu" )
                aInfo.frontCopper = copper;
            else if( name == "B.Cu" )
                aInfo.backCopper = copper;
            else
                aInfo.dielectricThickness += copper;
        }
        else if( name == "F.Mask" )
        {
            aInfo.frontMask = std::max( thickness, MIN_THICKNESS );
        }
        else if( name == "B.Mask" )
        {
            aInfo.backMask = std::max( thickness, MIN_THICKNESS );
        }
    }
}

} // namespace


std::vector<std::string> ExpandLayerWildcard( const std::string& aLayer, int aCopperLayerCount )
{
    std::vector<std::string> result;

    auto addCopper = [&]()
    {
        result.push_back( "F.Cu" );

        for( int i = 1; i < aCopperLayerCount - 1; ++i )
            result.push_back( "In" + std::to_string( i ) + ".Cu" );

        result.push_back( "B.Cu" );
    };

    if( aLayer == "*.Cu" )
    {
        addCopper();
    }
    else if( aLayer.rfind( "*.", 0 ) == 0 )
    {
        result.push_back( "F." + aLayer.substr( 2 ) );
        result.push_back( "B." + aLayer.substr( 2 ) );
    }
    else if( aLayer.rfind( "F&B.", 0 ) == 0 )
    {
        result.push_back( "F." + aLayer.substr( 4 ) );
        result.push_back( "B." + aLayer.substr( 4 ) );
    }
    else
    {
        result.push_back( aLayer );
    }

    return result;
}


Board ParseBoard( std::string_view aText )
{
    Node root;

    try
    {
        root = sexpr::Parse( aText );
    }
    catch( const sexpr::ParseError& e )
    {
        throw BoardReadError( std::string( "syntax error: " ) + e.what() );
    }

    if( root.keyword() != "kicad_pcb" )
        throw BoardReadError( "not a KiCad board file" );

    Board board;

    try
    {
        board.version = static_cast<int>( root.numberValue( "version" ).value_or( 0 ) );

        if( board.version < MIN_BOARD_VERSION )
        {
            board.warnings.push_back( "file format version " + std::to_string( board.version )
                                      + " is from KiCad 9 or older; KiRend is made for KiCad 10 "
                                        "files, so the result may be incomplete" );
        }

        if( const Node* general = root.find( "general" ) )
            board.generalThickness = general->numberValue( "thickness" ).value_or( 1.6 );

        if( const Node* layers = root.find( "layers" ) )
        {
            int copper = 0;

            for( const Node& layer : layers->children )
            {
                if( !layer.isList() || layer.children.size() < 3 )
                    continue;

                const std::string& name = layer.atom( 1 );

                if( name.size() > 3 && name.compare( name.size() - 3, 3, ".Cu" ) == 0 )
                    ++copper;

                if( layer.children.size() > 3 && layer.children[3].isAtom() )
                    board.layerAliases[layer.children[3].text] = name;
            }

            board.copperLayerCount = std::max( 2, copper );
        }

        if( const Node* setup = root.find( "setup" ) )
        {
            if( const Node* stackup = setup->find( "stackup" ) )
                readStackup( *stackup, board.stackup );
        }

        for( const Node& child : root.children )
        {
            if( !child.isList() )
                continue;

            const std::string& kw = child.keyword();

            if( kw == "footprint" )
            {
                board.footprints.push_back( readFootprint( child, board.copperLayerCount ) );
            }
            else if( kw == "segment" || kw == "arc" )
            {
                Track track;
                track.isArc = ( kw == "arc" );
                track.start = readPoint( child, "start" );
                track.end = readPoint( child, "end" );

                if( track.isArc )
                    track.mid = readPoint( child, "mid" );

                track.width = child.numberValue( "width" ).value_or( 0.0 );
                track.layer = child.value( "layer" ).value_or( "" );
                board.tracks.push_back( std::move( track ) );
            }
            else if( kw == "via" )
            {
                Via via;
                via.position = readPoint( child, "at" );
                via.size = child.numberValue( "size" ).value_or( 0.0 );
                via.drill = child.numberValue( "drill" ).value_or( 0.0 );

                if( const Node* layers = child.find( "layers" ) )
                {
                    for( size_t i = 1; i < layers->children.size(); ++i )
                    {
                        if( layers->children[i].isAtom() )
                            via.layers.push_back( layers->children[i].text );
                    }
                }

                board.vias.push_back( std::move( via ) );
            }
            else if( kw == "embedded_files" )
            {
                board.embeddedFiles = readEmbeddedFiles( child );
            }
            else
            {
                Shape shape;

                if( readShape( child, shape ) )
                    board.drawings.push_back( std::move( shape ) );
            }
        }
    }
    catch( const sexpr::ParseError& e )
    {
        throw BoardReadError( e.what() );
    }

    return board;
}


Board ReadBoardFile( const std::filesystem::path& aPath )
{
    std::ifstream in( aPath, std::ios::binary );

    if( !in )
        throw BoardReadError( "cannot open file '" + aPath.string() + "'" );

    std::ostringstream buffer;
    buffer << in.rdbuf();

    Board board = ParseBoard( buffer.str() );
    board.fileName = aPath.string();
    return board;
}

} // namespace kirend::pcb
