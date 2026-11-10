#include "render/camera.h"

#include <gtest/gtest.h>

using namespace kirend;
using namespace kirend::render;

TEST( Camera, BufferLayoutMatchesKiCad )
{
    // Values from KiCad's RENDER_3D_RAYTRACE_BASE::initializeBlockPositions() for 1600 x 900.
    BufferLayout layout = ComputeKiCadBufferLayout( 1600, 900 );
    EXPECT_EQ( layout.width, 1568 );
    EXPECT_EQ( layout.height, 872 );
    EXPECT_EQ( layout.xOffset, 16 );
    EXPECT_EQ( layout.yOffset, 14 );
}


TEST( Camera, BoardCentreProjectsToImageCentre )
{
    CameraSettings settings;
    Camera         camera( settings, { 50, 40 }, 100 );

    Projected p = camera.project( { 50, -40, 0 } );
    ASSERT_TRUE( p.valid );
    EXPECT_NEAR( p.u, camera.imageWidth() / 2.0, 1e-6 );
    EXPECT_NEAR( p.v, camera.imageHeight() / 2.0, 1e-6 );
}


TEST( Camera, TopViewScale )
{
    // Default top view: the larger board side spans 12.8 KiCad units, the visible height is
    // 2 * 16 * tan(22.5 deg) units over 900 pixels.
    CameraSettings settings;
    Camera         camera( settings, { 0, 0 }, 100 );

    const double expectedPxPerMm = 900.0 / ( 2.0 * 16.0 * std::tan( DegToRad( 22.5 ) ) ) * ( 12.8 / 100.0 );
    Projected    a = camera.project( { 0, 0, 0 } );
    Projected    b = camera.project( { 10, 0, 0 } );
    EXPECT_NEAR( b.u - a.u, 10 * expectedPxPerMm, 1e-6 );

    // +Y in the 3D frame (= -Y on the board) goes up in the image.
    Projected c = camera.project( { 0, 10, 0 } );
    EXPECT_LT( c.v, a.v );
}


TEST( Camera, ZoomIsClampedLikeKiCad )
{
    CameraSettings settings;
    settings.zoom = 0.25; // KiCad clamps the camera zoom at 2 (factor 0.5)
    Camera clamped( settings, { 0, 0 }, 100 );

    settings.zoom = 0.5;
    Camera half( settings, { 0, 0 }, 100 );

    EXPECT_NEAR( clamped.project( { 10, 0, 0 } ).u, half.project( { 10, 0, 0 } ).u, 1e-9 );
}


TEST( Camera, BottomViewMirrorsX )
{
    CameraSettings settings;
    settings.side = Side::Bottom;
    Camera camera( settings, { 0, 0 }, 100 );

    Projected a = camera.project( { 0, 0, 0 } );
    Projected b = camera.project( { 10, 0, 0 } );
    EXPECT_LT( b.u, a.u );
}
