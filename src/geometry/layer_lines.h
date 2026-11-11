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

/// What BuildLayerLines() draws.
struct LayerContent
{
    bool pads = true;
    bool tracks = true;
    bool vias = true;
    bool graphics = true;
};

/// Lines for one layer (everything except the board body): one line set each for graphics,
/// pads, tracks and vias, with the matching line category. Sets can be empty.
std::vector<render::LineSet> BuildLayerLines( const pcb::Board& aBoard, const std::string& aLayer,
                                              const BoardHeights& aHeights, const LayerContent& aContent = {} );

} // namespace kirend
