// 3D geometry of the bare board: body solid and layer heights, as KiCad's 3D viewer places them.
#pragma once

#include "geometry/outline.h"
#include "pcb/board.h"
#include "render/mesh.h"

#include <string>
#include <vector>

namespace kirend {

/// Z heights in mm (0 = middle of the board body), from BOARD_ADAPTER::InitSettings().
struct BoardHeights
{
    double bodyTop = 0.8;
    double bodyBottom = -0.8;
    double copperTopFront = 0.835;
    double copperTopBack = -0.835;
    double silkFront = 0.0;      ///< top of F.SilkS
    double silkBack = 0.0;       ///< "top" (outermost face) of B.SilkS
    double footprintFront = 0.0; ///< z of footprints on the front
    double footprintBack = 0.0;  ///< z of flipped footprints
};

BoardHeights ComputeBoardHeights( const pcb::Board& aBoard );

/// A drilled hole (pad or via) in board coordinates.
struct DrillHole
{
    Vec2   center;
    double sizeX = 0.0;
    double sizeY = 0.0;
    double angle = 0.0; ///< degrees, KiCad convention
};

std::vector<DrillHole> CollectDrillHoles( const pcb::Board& aBoard );

/// Polygon of a drill hole (circle or slot).
Polygon DrillHolePolygon( const DrillHole& aHole );

/// Convert a board coordinate (Y down) to the 3D frame (Y up) at height aZ.
inline Vec3 BoardTo3D( const Vec2& aPoint, double aZ ) { return { aPoint.x, -aPoint.y, aZ }; }

/// Triangle mesh of the board body with all holes cut. Lines come from crease detection.
/// @param aWarnings receives messages about problems with the outline.
render::Mesh BuildBoardBodyMesh( const BoardOutline& aOutline, const std::vector<DrillHole>& aHoles,
                                 const BoardHeights& aHeights, std::vector<std::string>& aWarnings );

} // namespace kirend
