#include "render/mesh.h"

namespace kirend::render {

void Mesh::append( const Mesh& aOther )
{
    const uint32_t base = static_cast<uint32_t>( vertices.size() );
    vertices.insert( vertices.end(), aOther.vertices.begin(), aOther.vertices.end() );

    triangles.reserve( triangles.size() + aOther.triangles.size() );

    for( const auto& t : aOther.triangles )
        triangles.push_back( { t[0] + base, t[1] + base, t[2] + base } );

    featureEdges.reserve( featureEdges.size() + aOther.featureEdges.size() );

    for( const auto& e : aOther.featureEdges )
        featureEdges.push_back( { e[0] + base, e[1] + base } );
}

} // namespace kirend::render
