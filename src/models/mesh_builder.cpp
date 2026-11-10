#include "models/mesh_builder.h"

namespace kirend::models {

Mat4 ModelTransform( const pcb::Footprint& aFootprint, const pcb::Model3D& aModel,
                     const BoardHeights& aHeights )
{
    const double z = aFootprint.flipped ? aHeights.footprintBack : aHeights.footprintFront;

    Mat4 m = Mat4::Translation( { aFootprint.position.x, -aFootprint.position.y, z } );

    if( aFootprint.angle != 0.0 )
        m = m.rotated( DegToRad( aFootprint.angle ), { 0, 0, 1 } );

    if( aFootprint.flipped )
    {
        m = m.rotated( PI, { 0, 1, 0 } );
        m = m.rotated( PI, { 0, 0, 1 } );
    }

    m = m.translated( aModel.offset );
    m = m.rotated( -DegToRad( aModel.rotation.z ), { 0, 0, 1 } );
    m = m.rotated( -DegToRad( aModel.rotation.y ), { 0, 1, 0 } );
    m = m.rotated( -DegToRad( aModel.rotation.x ), { 1, 0, 0 } );
    m = m.scaled( aModel.scale );
    return m;
}


render::Mesh TransformMesh( const render::Mesh& aMesh, const Mat4& aTransform )
{
    render::Mesh out;
    out.vertices.reserve( aMesh.vertices.size() );

    for( const Vec3& v : aMesh.vertices )
        out.vertices.push_back( aTransform.transformPoint( v ) );

    out.triangles = aMesh.triangles;
    out.featureEdges = aMesh.featureEdges;
    return out;
}

} // namespace kirend::models
