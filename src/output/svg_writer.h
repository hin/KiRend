#pragma once

#include "render/line_renderer.h"
#include "style/style.h"

#include <string>
#include <vector>

namespace kirend::output {

/// Write the lines as SVG. Line widths in aStyle (points) are converted to pixels with aDpi.
/// aStyle.background nullopt gives a transparent background.
std::string WriteSvg( const std::vector<render::Polyline2D>& aLines, int aWidth, int aHeight,
                      const style::Style& aStyle, double aDpi );

} // namespace kirend::output
