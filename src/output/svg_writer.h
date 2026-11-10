#pragma once

#include "render/line_renderer.h"

#include <string>
#include <vector>

namespace kirend::output {

enum class Background { White, Transparent };

struct SvgStyle
{
    double     lineWidth = 1.5;    ///< px, inner edges
    double     outlineWidth = 3.0; ///< px, silhouettes and outer edges
    Background background = Background::White;
};

/// Default line widths for an image of the given height (1.5 / 3 px at 900 px).
SvgStyle DefaultStyle( int aImageHeight );

std::string WriteSvg( const std::vector<render::Polyline2D>& aLines, int aWidth, int aHeight,
                      const SvgStyle& aStyle );

} // namespace kirend::output
