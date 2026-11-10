#pragma once

#include <filesystem>
#include <string>

namespace kirend::output {

/// Rasterise an SVG document to a PNG file of the given size, tagged with aDpi.
/// Throws std::runtime_error on failure.
void WritePng( const std::string& aSvg, int aWidth, int aHeight, double aDpi,
               const std::filesystem::path& aPath );

} // namespace kirend::output
