#include "render/line_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace kirend::render {

namespace {

constexpr float FAR_DEPTH = std::numeric_limits<float>::infinity();

struct Edge
{
    uint32_t v0;
    uint32_t v1;
    int64_t  t0 = -1;
    int64_t  t1 = -1;
    bool     nonManifold = false;
    bool     feature = false;
};

/// Mesh with merged duplicate vertices and edge adjacency.
struct PreparedMesh
{
    std::vector<Vec3>                    vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    std::vector<Edge>                    edges;
};


uint64_t edgeKey( uint32_t a, uint32_t b )
{
    if( a > b )
        std::swap( a, b );

    return ( static_cast<uint64_t>( a ) << 32 ) | b;
}


PreparedMesh prepareMesh( const Mesh& aMesh, double aCreaseAngleDeg )
{
    PreparedMesh out;

    // Weld vertices that share a position (OCCT stores each face separately).
    constexpr double QUANTUM = 1e-5; // mm
    struct KeyHash
    {
        size_t operator()( const std::array<int64_t, 3>& k ) const
        {
            uint64_t h = static_cast<uint64_t>( k[0] ) * 73856093ULL;
            h ^= static_cast<uint64_t>( k[1] ) * 19349663ULL;
            h ^= static_cast<uint64_t>( k[2] ) * 83492791ULL;
            return static_cast<size_t>( h );
        }
    };

    std::unordered_map<std::array<int64_t, 3>, uint32_t, KeyHash> lookup;
    lookup.reserve( aMesh.vertices.size() );
    std::vector<uint32_t> remap( aMesh.vertices.size() );

    for( size_t i = 0; i < aMesh.vertices.size(); ++i )
    {
        const Vec3&                   v = aMesh.vertices[i];
        const std::array<int64_t, 3> key{ std::llround( v.x / QUANTUM ), std::llround( v.y / QUANTUM ),
                                          std::llround( v.z / QUANTUM ) };

        auto [it, inserted] = lookup.try_emplace( key, static_cast<uint32_t>( out.vertices.size() ) );

        if( inserted )
            out.vertices.push_back( v );

        remap[i] = it->second;
    }

    std::unordered_map<uint64_t, uint32_t> edgeIndex;
    edgeIndex.reserve( aMesh.triangles.size() * 2 );

    auto addEdge = [&]( uint32_t a, uint32_t b ) -> Edge&
    {
        auto [it, inserted] = edgeIndex.try_emplace( edgeKey( a, b ), static_cast<uint32_t>( out.edges.size() ) );

        if( inserted )
        {
            Edge e;
            e.v0 = a;
            e.v1 = b;
            out.edges.push_back( e );
        }

        return out.edges[it->second];
    };

    for( const auto& tri : aMesh.triangles )
    {
        std::array<uint32_t, 3> t{ remap[tri[0]], remap[tri[1]], remap[tri[2]] };

        if( t[0] == t[1] || t[1] == t[2] || t[0] == t[2] )
            continue; // degenerate after welding

        const int64_t index = static_cast<int64_t>( out.triangles.size() );
        out.triangles.push_back( t );

        for( int k = 0; k < 3; ++k )
        {
            Edge& e = addEdge( t[k], t[( k + 1 ) % 3] );

            if( e.t0 < 0 )
                e.t0 = index;
            else if( e.t1 < 0 )
                e.t1 = index;
            else
                e.nonManifold = true;
        }
    }

    for( const auto& fe : aMesh.featureEdges )
    {
        uint32_t a = remap[fe[0]];
        uint32_t b = remap[fe[1]];

        if( a == b )
            continue;

        addEdge( a, b ).feature = true;
    }

    // Creases: angle between the planes of the two adjacent triangles (orientation independent,
    // because WRL models often have inconsistent winding).
    const double cosCrease = std::cos( aCreaseAngleDeg * 3.14159265358979323846 / 180.0 );

    auto normalOf = [&]( int64_t ti )
    {
        const auto& t = out.triangles[ti];
        return Normalize( Cross( out.vertices[t[1]] - out.vertices[t[0]],
                                 out.vertices[t[2]] - out.vertices[t[0]] ) );
    };

    for( Edge& e : out.edges )
    {
        if( e.feature || e.t0 < 0 || e.t1 < 0 || e.nonManifold )
            continue;

        if( std::abs( Dot( normalOf( e.t0 ), normalOf( e.t1 ) ) ) < cosCrease )
            e.feature = true;
    }

    return out;
}


class DepthBuffer
{
public:
    DepthBuffer( int aWidth, int aHeight, int aSupersample ) :
            m_width( aWidth * aSupersample ),
            m_height( aHeight * aSupersample ),
            m_scale( aSupersample ),
            m_data( static_cast<size_t>( m_width ) * m_height, FAR_DEPTH )
    {
    }

    /// Rasterise a triangle given in output pixel coordinates.
    void drawTriangle( const Projected& a, const Projected& b, const Projected& c, bool aPerspective )
    {
        const double ax = a.u * m_scale, ay = a.v * m_scale;
        const double bx = b.u * m_scale, by = b.v * m_scale;
        const double cx = c.u * m_scale, cy = c.v * m_scale;

        const double area = ( bx - ax ) * ( cy - ay ) - ( by - ay ) * ( cx - ax );

        if( std::abs( area ) < 1e-12 )
            return;

        const int x0 = std::max( 0, static_cast<int>( std::floor( std::min( { ax, bx, cx } ) ) ) );
        const int x1 = std::min( m_width - 1, static_cast<int>( std::ceil( std::max( { ax, bx, cx } ) ) ) );
        const int y0 = std::max( 0, static_cast<int>( std::floor( std::min( { ay, by, cy } ) ) ) );
        const int y1 = std::min( m_height - 1, static_cast<int>( std::ceil( std::max( { ay, by, cy } ) ) ) );

        if( x0 > x1 || y0 > y1 )
            return;

        // Perspective: 1/depth is linear in screen space; orthographic: depth is.
        const double za = aPerspective ? 1.0 / a.depth : a.depth;
        const double zb = aPerspective ? 1.0 / b.depth : b.depth;
        const double zc = aPerspective ? 1.0 / c.depth : c.depth;
        const double invArea = 1.0 / area;

        for( int y = y0; y <= y1; ++y )
        {
            const double py = y + 0.5;
            float*       row = &m_data[static_cast<size_t>( y ) * m_width];

            for( int x = x0; x <= x1; ++x )
            {
                const double px = x + 0.5;
                double       w0 = ( ( bx - px ) * ( cy - py ) - ( by - py ) * ( cx - px ) ) * invArea;
                double       w1 = ( ( cx - px ) * ( ay - py ) - ( cy - py ) * ( ax - px ) ) * invArea;
                double       w2 = 1.0 - w0 - w1;

                if( w0 < 0 || w1 < 0 || w2 < 0 )
                    continue;

                double z = w0 * za + w1 * zb + w2 * zc;
                float  depth = static_cast<float>( aPerspective ? 1.0 / z : z );

                if( depth < row[x] )
                    row[x] = depth;
            }
        }
    }

    /// True if a point at the given output pixel position and depth is not hidden.
    /// Returns false for points outside the image.
    bool visible( double aU, double aV, double aDepth, double aTolerance ) const
    {
        const double x = aU * m_scale;
        const double y = aV * m_scale;

        if( x < 0 || y < 0 || x >= m_width || y >= m_height )
            return false;

        const int ix = static_cast<int>( x );
        const int iy = static_cast<int>( y );

        // A line on a surface is visible if any neighbouring sample isn't in front of it; this
        // covers sub-sample depth slopes and edges exactly on silhouettes.
        for( int dy = -1; dy <= 1; ++dy )
        {
            const int yy = iy + dy;

            if( yy < 0 || yy >= m_height )
                return true;

            for( int dx = -1; dx <= 1; ++dx )
            {
                const int xx = ix + dx;

                if( xx < 0 || xx >= m_width )
                    return true;

                if( m_data[static_cast<size_t>( yy ) * m_width + xx] >= aDepth - aTolerance )
                    return true;
            }
        }

        return false;
    }

private:
    int                m_width;
    int                m_height;
    int                m_scale;
    std::vector<float> m_data;
};


struct Segment2D
{
    Vec2 a;
    Vec2 b;
    bool outline;
};


class LineCollector
{
public:
    LineCollector( const Camera& aCamera, const DepthBuffer& aDepth, double aTolerance, int aSupersample ) :
            m_camera( aCamera ),
            m_depth( aDepth ),
            m_tolerance( aTolerance ),
            m_supersample( aSupersample )
    {
    }

    void addSegment( const Vec3& aA, const Vec3& aB, bool aOutline )
    {
        Vec3      a = aA;
        Vec3      b = aB;
        Projected pa = m_camera.project( a );
        Projected pb = m_camera.project( b );

        if( !pa.valid && !pb.valid )
            return;

        // Clip against the near plane (perspective only); depth is linear along the segment.
        if( !pa.valid || !pb.valid )
        {
            const double nearDepth = 0.1;
            double       t = ( nearDepth - pa.depth ) / ( pb.depth - pa.depth );

            if( !pa.valid )
            {
                a = a + ( b - a ) * t;
                pa = m_camera.project( a );
            }
            else
            {
                b = a + ( b - a ) * t;
                pb = m_camera.project( b );
            }

            if( !pa.valid || !pb.valid )
                return;
        }

        const double screenLen = std::hypot( pb.u - pa.u, pb.v - pa.v ) * m_supersample;
        const int    steps = std::max( 1, static_cast<int>( std::ceil( screenLen * 2.0 ) ) );

        int  runStart = -1;
        auto flush = [&]( int aEnd )
        {
            if( runStart < 0 )
                return;

            const double t0 = static_cast<double>( runStart ) / steps;
            const double t1 = static_cast<double>( aEnd ) / steps;

            if( aEnd > runStart )
            {
                Projected q0 = ( runStart == 0 ) ? pa : m_camera.project( a + ( b - a ) * t0 );
                Projected q1 = ( aEnd == steps ) ? pb : m_camera.project( a + ( b - a ) * t1 );
                m_segments.push_back( { { q0.u, q0.v }, { q1.u, q1.v }, aOutline } );
            }

            runStart = -1;
        };

        for( int k = 0; k <= steps; ++k )
        {
            Projected q;

            if( k == 0 )
                q = pa;
            else if( k == steps )
                q = pb;
            else
                q = m_camera.project( a + ( b - a ) * ( static_cast<double>( k ) / steps ) );

            const bool vis = q.valid && m_depth.visible( q.u, q.v, q.depth, m_tolerance );

            if( vis && runStart < 0 )
                runStart = k;
            else if( !vis && runStart >= 0 )
                flush( k - 1 );
        }

        flush( steps );
    }

    std::vector<Segment2D>& segments() { return m_segments; }

private:
    const Camera&          m_camera;
    const DepthBuffer&     m_depth;
    double                 m_tolerance;
    int                    m_supersample;
    std::vector<Segment2D> m_segments;
};


double pointSegmentDistance( const Vec2& p, const Vec2& a, const Vec2& b )
{
    const Vec2   ab = b - a;
    const double len2 = Dot( ab, ab );

    if( len2 <= 0 )
        return ( p - a ).length();

    const double t = std::clamp( Dot( p - a, ab ) / len2, 0.0, 1.0 );
    return ( p - ( a + ab * t ) ).length();
}


void simplify( const std::vector<Vec2>& aIn, double aTolerance, std::vector<Vec2>& aOut )
{
    if( aIn.size() <= 2 )
    {
        aOut = aIn;
        return;
    }

    std::vector<bool>                   keep( aIn.size(), false );
    std::vector<std::pair<size_t, size_t>> stack{ { 0, aIn.size() - 1 } };
    keep.front() = keep.back() = true;

    while( !stack.empty() )
    {
        auto [first, last] = stack.back();
        stack.pop_back();

        double maxDist = -1;
        size_t index = first;

        for( size_t i = first + 1; i < last; ++i )
        {
            double d = pointSegmentDistance( aIn[i], aIn[first], aIn[last] );

            if( d > maxDist )
            {
                maxDist = d;
                index = i;
            }
        }

        if( maxDist > aTolerance )
        {
            keep[index] = true;
            stack.push_back( { first, index } );
            stack.push_back( { index, last } );
        }
    }

    aOut.clear();

    for( size_t i = 0; i < aIn.size(); ++i )
    {
        if( keep[i] )
            aOut.push_back( aIn[i] );
    }
}


/// Join segments that share end points into polylines.
std::vector<Polyline2D> chainSegments( const std::vector<Segment2D>& aSegments, double aSimplifyTolerance )
{
    std::vector<Polyline2D> result;

    for( int pass = 0; pass < 2; ++pass )
    {
        const bool outline = ( pass == 1 );

        std::vector<const Segment2D*> segs;

        for( const Segment2D& s : aSegments )
        {
            if( s.outline == outline && ( s.a - s.b ).length() > 1e-9 )
                segs.push_back( &s );
        }

        auto key = []( const Vec2& p )
        {
            const int64_t x = std::llround( p.x * 1000.0 );
            const int64_t y = std::llround( p.y * 1000.0 );
            return ( static_cast<uint64_t>( x ) * 0x9E3779B97F4A7C15ULL ) ^ static_cast<uint64_t>( y );
        };

        std::unordered_multimap<uint64_t, size_t> ends;
        ends.reserve( segs.size() * 2 );

        for( size_t i = 0; i < segs.size(); ++i )
        {
            ends.emplace( key( segs[i]->a ), i );
            ends.emplace( key( segs[i]->b ), i );
        }

        std::vector<bool> used( segs.size(), false );

        auto takeNext = [&]( const Vec2& aPoint, Vec2& aOther ) -> bool
        {
            auto range = ends.equal_range( key( aPoint ) );

            for( auto it = range.first; it != range.second; ++it )
            {
                if( used[it->second] )
                    continue;

                const Segment2D* s = segs[it->second];

                if( ( s->a - aPoint ).length() < 2e-3 )
                    aOther = s->b;
                else if( ( s->b - aPoint ).length() < 2e-3 )
                    aOther = s->a;
                else
                    continue;

                used[it->second] = true;
                return true;
            }

            return false;
        };

        std::vector<Vec2> forward;
        std::vector<Vec2> backward;

        for( size_t i = 0; i < segs.size(); ++i )
        {
            if( used[i] )
                continue;

            used[i] = true;
            forward = { segs[i]->a, segs[i]->b };
            backward.clear();

            Vec2 next;

            while( takeNext( forward.back(), next ) )
                forward.push_back( next );

            Vec2 head = forward.front();

            while( takeNext( head, next ) )
            {
                backward.push_back( next );
                head = next;
            }

            std::vector<Vec2> chain( backward.rbegin(), backward.rend() );
            chain.insert( chain.end(), forward.begin(), forward.end() );

            Polyline2D line;
            line.outline = outline;
            simplify( chain, aSimplifyTolerance, line.points );
            result.push_back( std::move( line ) );
        }
    }

    return result;
}

} // namespace


std::vector<Polyline2D> RenderLines( const Scene& aScene, const Camera& aCamera,
                                     const LineRenderOptions& aOptions )
{
    const int    S = std::max( 1, aOptions.supersample );
    const bool   persp = aCamera.perspective();
    const double tolerance = aOptions.depthToleranceMm * aCamera.mmTo3D();

    std::vector<PreparedMesh>          prepared;
    std::vector<std::vector<Projected>> projected;
    prepared.reserve( aScene.meshes.size() );

    DepthBuffer depth( aCamera.imageWidth(), aCamera.imageHeight(), S );

    for( const Mesh& mesh : aScene.meshes )
    {
        prepared.push_back( prepareMesh( mesh, aOptions.creaseAngleDeg ) );
        const PreparedMesh& pm = prepared.back();

        std::vector<Projected> proj( pm.vertices.size() );

        for( size_t i = 0; i < pm.vertices.size(); ++i )
            proj[i] = aCamera.project( pm.vertices[i] );

        for( const auto& t : pm.triangles )
        {
            if( proj[t[0]].valid && proj[t[1]].valid && proj[t[2]].valid )
                depth.drawTriangle( proj[t[0]], proj[t[1]], proj[t[2]], persp );
        }

        projected.push_back( std::move( proj ) );
    }

    LineCollector collector( aCamera, depth, tolerance, S );

    for( size_t m = 0; m < prepared.size(); ++m )
    {
        const PreparedMesh&           pm = prepared[m];
        const std::vector<Projected>& proj = projected[m];

        auto thirdVertex = [&]( int64_t aTri, const Edge& aEdge )
        {
            for( uint32_t v : pm.triangles[aTri] )
            {
                if( v != aEdge.v0 && v != aEdge.v1 )
                    return v;
            }

            return pm.triangles[aTri][0];
        };

        for( const Edge& e : pm.edges )
        {
            bool boundary = ( e.t0 >= 0 && e.t1 < 0 ) || e.nonManifold;
            bool silhouette = false;

            if( e.t0 >= 0 && e.t1 >= 0 && !e.nonManifold )
            {
                // Silhouette if both neighbouring triangles lie on the same side of the edge on
                // screen, i.e. the surface folds away from the viewer here.
                const Projected& p0 = proj[e.v0];
                const Projected& p1 = proj[e.v1];
                const Projected& pa = proj[thirdVertex( e.t0, e )];
                const Projected& pb = proj[thirdVertex( e.t1, e )];

                if( p0.valid && p1.valid && pa.valid && pb.valid )
                {
                    const double dx = p1.u - p0.u;
                    const double dy = p1.v - p0.v;
                    const double sa = dx * ( pa.v - p0.v ) - dy * ( pa.u - p0.u );
                    const double sb = dx * ( pb.v - p0.v ) - dy * ( pb.u - p0.u );
                    silhouette = ( sa * sb >= 0 );
                }
            }

            if( !e.feature && !boundary && !silhouette )
                continue;

            collector.addSegment( pm.vertices[e.v0], pm.vertices[e.v1], silhouette || boundary );
        }
    }

    for( const LineSet& set : aScene.lines )
    {
        for( const std::vector<Vec3>& poly : set.polylines )
        {
            for( size_t i = 0; i + 1 < poly.size(); ++i )
                collector.addSegment( poly[i], poly[i + 1], false );
        }
    }

    return chainSegments( collector.segments(), aOptions.simplifyTolerancePx );
}

} // namespace kirend::render
