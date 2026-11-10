// Camera that reproduces the framing of `kicad-cli pcb render` exactly.
#pragma once

#include "geometry/vec.h"

#include <string>

namespace kirend::render {

enum class Side { Top, Bottom, Left, Right, Front, Back };

struct CameraSettings
{
    int    width = 1600;   ///< Requested image width (-w)
    int    height = 900;   ///< Requested image height (-h)
    Side   side = Side::Top;
    bool   perspective = false;
    double zoom = 1.0;
    Vec3   pan;            ///< --pan, in KiCad 3D-viewer units
    Vec3   pivot;          ///< --pivot, in cm relative to the board centre
    Vec3   rotation;       ///< --rotate, degrees
};

struct Projected
{
    double u = 0.0;     ///< Image x in pixels (0 = left edge of the output image)
    double v = 0.0;     ///< Image y in pixels (0 = top edge of the output image)
    double depth = 0.0; ///< Distance along the view direction (KiCad 3D units)
    bool   valid = true;
};

/// Output buffer size of KiCad's ray tracer for a requested window size. kicad-cli writes images
/// of exactly this size, which is a little smaller than -w x -h.
struct BufferLayout
{
    int width = 0;
    int height = 0;
    int xOffset = 0;
    int yOffset = 0;
};

BufferLayout ComputeKiCadBufferLayout( int aWindowWidth, int aWindowHeight );

class Camera
{
public:
    /// @param aBoardCenterMm Centre of the board bounding box in KiCad board coordinates.
    /// @param aBoardMaxSizeMm Larger side of the board bounding box.
    Camera( const CameraSettings& aSettings, const Vec2& aBoardCenterMm, double aBoardMaxSizeMm );

    /// Project a world point (mm, 3D frame: X right, Y up, Z up from board centre plane).
    Projected project( const Vec3& aWorldMm ) const;

    int  imageWidth() const { return m_layout.width; }
    int  imageHeight() const { return m_layout.height; }
    bool perspective() const { return m_settings.perspective; }

    /// Scale from mm to KiCad 3D units.
    double mmTo3D() const { return m_mmTo3D; }

    /// Direction from the scene towards the camera (orthographic), or camera position (3D units).
    Vec3 cameraPosition3D() const { return m_pos; }
    Vec3 viewDirection() const { return m_dir; } ///< points from the scene towards the camera

    /// Approximate size of one output pixel in mm at the depth of the board centre.
    double pixelSizeMm() const;

private:
    CameraSettings m_settings;
    BufferLayout   m_layout;
    double         m_mmTo3D = 1.0;
    Mat4           m_view;
    double         m_ratio = 1.0;
    double         m_orthoHalfHeight = 1.0; ///< ortho: half height of view volume (3D units)
    double         m_tang = 0.0;            ///< tan(fov/2)
    double         m_randDisp = 0.0;
    Vec3           m_pos;
    Vec3           m_dir;
    double         m_lookatDepth = 16.0;
};

} // namespace kirend::render
