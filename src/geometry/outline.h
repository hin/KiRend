// Board outline extraction from Edge.Cuts shapes.
#pragma once

#include "geometry/vec.h"
#include "pcb/board.h"

#include <optional>
#include <string>
#include <vector>

namespace kirend {

/// Axis-aligned box in board coordinates (mm).
struct Box2
{
    Vec2 min{ 1e300, 1e300 };
    Vec2 max{ -1e300, -1e300 };

    bool valid() const { return min.x <= max.x && min.y <= max.y; }
    void merge( const Vec2& p );
    void merge( const Box2& b );
    void inflate( double d );
    Vec2 center() const { return ( min + max ) / 2.0; }
    Vec2 size() const { return max - min; }
};

/// A closed polygon. The first point is not repeated at the end.
using Polygon = std::vector<Vec2>;

/// One board piece: an outer contour and the cut-outs inside it.
struct OutlinePiece
{
    Polygon              outer;
    std::vector<Polygon> holes;
};

struct BoardOutline
{
    std::vector<OutlinePiece> pieces;
    bool                      fromEdgeCuts = false; ///< false: fallback bounding box used
    std::string               warning;
};

/// Bounding box of one shape, the same way KiCad computes it (stroke width included).
Box2 ShapeBoundingBox( const pcb::Shape& aShape );

/// Turn a closed shape (circle, rect, polygon) into a polygon; nullopt for open shapes.
std::optional<Polygon> ClosedShapeToPolygon( const pcb::Shape& aShape );

/// Polyline for an open shape (segment, arc, bezier).
std::vector<Vec2> OpenShapeToPolyline( const pcb::Shape& aShape );

/// All Edge.Cuts shapes of the board, including those inside footprints.
std::vector<const pcb::Shape*> EdgeCutShapes( const pcb::Board& aBoard );

/// Chain Edge.Cuts into closed contours and sort them into outlines and holes.
BoardOutline BuildBoardOutline( const pcb::Board& aBoard );

/// The board bounding box KiCad's 3D viewer uses for framing (see BOARD::ComputeBoundingBox).
Box2 BoardBoundingBox( const pcb::Board& aBoard, bool aHaveOutline );

/// Signed area (positive = counter-clockwise in a Y-up system).
double SignedArea( const Polygon& aPoly );

/// Point in polygon test (even-odd rule).
bool PointInPolygon( const Vec2& aPoint, const Polygon& aPoly );

} // namespace kirend
