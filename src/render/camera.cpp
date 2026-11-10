#include "render/camera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kirend::render {

namespace {

// Constants from KiCad's 3D viewer (board_adapter.h, camera.cpp, track_ball.cpp).
constexpr double RANGE_SCALE_3D = 8.0;
constexpr double INITIAL_DISTANCE = 2.0 * RANGE_SCALE_3D; // TRACK_BALL camera( 2 * RANGE_SCALE_3D )
constexpr double MIN_ZOOM = 0.020;
constexpr double MAX_ZOOM = 2.0;
constexpr double FOV_DEGREES = 45.0;
constexpr double NEAR_PERSPECTIVE = 0.10;
constexpr double PERSPECTIVE_DISPLACEMENT = 0.075; // DISP_FACTOR in render_3d_raytrace_base.cpp
constexpr int    RAYPACKET_DIM = 8;

uint32_t compact1By1( uint32_t x )
{
    x &= 0x55555555;
    x = ( x ^ ( x >> 1 ) ) & 0x33333333;
    x = ( x ^ ( x >> 2 ) ) & 0x0f0f0f0f;
    x = ( x ^ ( x >> 4 ) ) & 0x00ff00ff;
    x = ( x ^ ( x >> 8 ) ) & 0x0000ffff;
    return x;
}

} // namespace


BufferLayout ComputeKiCadBufferLayout( int aWindowWidth, int aWindowHeight )
{
    // Mirrors RENDER_3D_RAYTRACE_BASE::initializeBlockPositions().
    const unsigned int limitX = static_cast<unsigned int>( aWindowWidth ) - ( 4 * RAYPACKET_DIM + 4 );
    const unsigned int limitY = static_cast<unsigned int>( aWindowHeight ) - ( 4 * RAYPACKET_DIM + 4 );

    unsigned int maxX = 0;
    unsigned int maxY = 0;

    for( uint32_t i = 0;; ++i )
    {
        const unsigned int mX = compact1By1( i );
        const unsigned int mY = compact1By1( i >> 1 );
        const unsigned int bx = mX * 4 * RAYPACKET_DIM - mX * 4;
        const unsigned int by = mY * 4 * RAYPACKET_DIM - mY * 4;

        if( bx >= limitX && by >= limitY )
            break;

        if( bx < limitX && by < limitY )
        {
            maxX = std::max( maxX, bx );
            maxY = std::max( maxY, by );
        }
    }

    BufferLayout layout;
    layout.width = static_cast<int>( ( maxX + RAYPACKET_DIM * 4 ) & ~( RAYPACKET_DIM - 1u ) );
    layout.height = static_cast<int>( ( maxY + RAYPACKET_DIM * 4 ) & ~( RAYPACKET_DIM - 1u ) );
    layout.xOffset = ( aWindowWidth - layout.width ) / 2;
    layout.yOffset = ( aWindowHeight - layout.height ) / 2;
    return layout;
}


Camera::Camera( const CameraSettings& aSettings, const Vec2& aBoardCenterMm, double aBoardMaxSizeMm ) :
        m_settings( aSettings )
{
    m_layout = ComputeKiCadBufferLayout( aSettings.width, aSettings.height );

    // BOARD_ADAPTER::InitSettings(): biuTo3Dunits = RANGE_SCALE_3D / max(size) * 1.6.
    m_mmTo3D = RANGE_SCALE_3D / aBoardMaxSizeMm * 1.6;

    // Look-at point: board centre (Y inverted in the 3D viewer), plus --pivot (cm).
    const double cmTo3D = m_mmTo3D * 10.0;
    const Vec3   lookat( aBoardCenterMm.x * m_mmTo3D + aSettings.pivot.x * cmTo3D,
                         -aBoardCenterMm.y * m_mmTo3D + aSettings.pivot.y * cmTo3D,
                         0.0 + aSettings.pivot.z * cmTo3D );

    // Camera position: initial (0, 0, -distance), plus --pan, then zoom replaces Z.
    Vec3   camPos( aSettings.pan.x, aSettings.pan.y, -INITIAL_DISTANCE + aSettings.pan.z );
    double zoom = 1.0;

    // CAMERA::Zoom_T1() ignores a factor of exactly 1.
    if( aSettings.zoom != 1.0 && aSettings.zoom > 0.0 )
    {
        zoom = std::clamp( 1.0 / aSettings.zoom, MIN_ZOOM, MAX_ZOOM );
        camPos.z = -INITIAL_DISTANCE * zoom;
    }

    // Rotation: view preset (CAMERA::ViewCommand_T1) plus --rotate, accumulated per axis.
    Vec3 rot;
    const double epsilon180 = DegToRad( 179.999 );

    switch( aSettings.side )
    {
    case Side::Top: break;
    case Side::Bottom: rot.y += epsilon180; break;
    case Side::Front: rot.x += DegToRad( -90.0 ); break;
    case Side::Back:
        rot.x += DegToRad( -90.0 );
        rot.z += epsilon180;
        break;
    case Side::Left:
        rot.z += DegToRad( 90.0 );
        rot.x += DegToRad( -90.0 );
        break;
    case Side::Right:
        rot.z += DegToRad( -90.0 );
        rot.x += DegToRad( -90.0 );
        break;
    }

    rot.x += DegToRad( aSettings.rotation.x );
    rot.y += DegToRad( aSettings.rotation.y );
    rot.z += DegToRad( aSettings.rotation.z );

    // CAMERA::updateRotationMatrix(): rotate X, then Y, then Z (glm post-multiplication).
    const Mat4 rotation = Mat4::Identity()
                                  .rotated( rot.x, Vec3( 1, 0, 0 ) )
                                  .rotated( rot.y, Vec3( 0, 1, 0 ) )
                                  .rotated( rot.z, Vec3( 0, 0, 1 ) );

    // CAMERA::updateViewMatrix()
    m_view = Mat4::Translation( camPos ) * rotation * Mat4::Translation( -lookat );

    const Mat4 inv = m_view.inverseAffine();
    m_pos = inv.transformPoint( Vec3( 0, 0, 0 ) );
    m_dir = Normalize( inv.transformVector( Vec3( 0, 0, 1 ) ) );

    // CAMERA::rebuildProjection()
    m_ratio = static_cast<double>( aSettings.width ) / static_cast<double>( aSettings.height );
    m_tang = std::tan( DegToRad( FOV_DEGREES ) * 0.5 );
    m_orthoHalfHeight = INITIAL_DISTANCE * zoom * m_tang;
    m_randDisp = aSettings.perspective ? PERSPECTIVE_DISPLACEMENT : 0.0;
    m_lookatDepth = -camPos.z;
}


Projected Camera::project( const Vec3& aWorldMm ) const
{
    const Vec3 q = m_view.transformPoint( aWorldMm * m_mmTo3D );

    Projected p;
    p.depth = -q.z;

    double nx;
    double ny;

    if( m_settings.perspective )
    {
        if( p.depth <= NEAR_PERSPECTIVE * 0.5 )
        {
            p.valid = false;
            return p;
        }

        nx = q.x / ( p.depth * m_tang * m_ratio );
        ny = q.y / ( p.depth * m_tang );
    }
    else
    {
        nx = q.x / ( m_ratio * m_orthoHalfHeight );
        ny = q.y / m_orthoHalfHeight;
    }

    // Map normalised device coordinates to output image pixels: see CAMERA::MakeRay(), the
    // block offset in RENDER_3D_RAYTRACE_BASE and the vertical mirror in the render job.
    const double windowX = ( nx + 1.0 ) * 0.5 * m_settings.width;
    const double windowY = ( ny + 1.0 ) * 0.5 * m_settings.height;

    p.u = windowX - m_layout.xOffset - m_randDisp;
    p.v = m_layout.height - windowY + m_layout.yOffset + m_randDisp;
    return p;
}


double Camera::pixelSizeMm() const
{
    double viewHeight3D;

    if( m_settings.perspective )
        viewHeight3D = 2.0 * m_lookatDepth * m_tang;
    else
        viewHeight3D = 2.0 * m_orthoHalfHeight;

    return viewHeight3D / m_settings.height / m_mmTo3D;
}

} // namespace kirend::render
