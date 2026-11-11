// Hidden-line renderer: finds feature, boundary and silhouette edges, removes the hidden parts
// with a depth buffer and returns 2D polylines in output image pixels.
#pragma once

#include "render/camera.h"
#include "render/mesh.h"

#include <vector>

namespace kirend::render {

struct Polyline2D
{
    std::vector<Vec2> points; ///< Output image pixels
    bool              outline = false; ///< Silhouette / outer edge (drawn thicker)
    style::LineCategory category = style::LineCategory::Models;
};

struct LineRenderOptions
{
    int    supersample = 3;          ///< Depth buffer resolution relative to the output image
    double creaseAngleDeg = 30.0;    ///< Mesh edges sharper than this are drawn (WRL models)
    double depthToleranceMm = 0.005; ///< Lines this close behind a surface still count as visible
    double simplifyTolerancePx = 0.05;
};

std::vector<Polyline2D> RenderLines( const Scene& aScene, const Camera& aCamera,
                                     const LineRenderOptions& aOptions = {} );

} // namespace kirend::render
