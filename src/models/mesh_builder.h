// Places footprint 3D models on the board, using the same transform as KiCad's 3D viewer.
#pragma once

#include "geometry/board_geometry.h"
#include "pcb/board.h"
#include "render/mesh.h"

namespace kirend::models {

/// Transform from model space (mm) to world space (mm), see RENDER_3D_RAYTRACE_BASE::load3DModels().
Mat4 ModelTransform( const pcb::Footprint& aFootprint, const pcb::Model3D& aModel,
                     const BoardHeights& aHeights );

/// Copy of aMesh with aTransform applied.
render::Mesh TransformMesh( const render::Mesh& aMesh, const Mat4& aTransform );

} // namespace kirend::models
