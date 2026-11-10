// 2D line work for board layers (pads, tracks, vias, graphics), lifted to the layer's height.
#pragma once

#include "geometry/board_geometry.h"
#include "pcb/board.h"
#include "render/mesh.h"

#include <set>
#include <string>
#include <vector>

namespace kirend {

/// Canonical KiCad layer name for a user-supplied name ("F.Silkscreen" -> "F.SilkS").
/// Returns an empty string for unknown names.
std::string CanonicalLayerName( const std::string& aName, const pcb::Board& aBoard );

/// Polygon of a pad's copper shape in board coordinates.
std::vector<Polygon> PadPolygons( const pcb::Pad& aPad );

/// Lines for one layer (everything except the board body).
render::LineSet BuildLayerLines( const pcb::Board& aBoard, const std::string& aLayer,
                                 const BoardHeights& aHeights );

} // namespace kirend
