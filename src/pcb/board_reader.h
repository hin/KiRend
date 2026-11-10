#pragma once

#include "pcb/board.h"

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace kirend::pcb {

class BoardReadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Read a .kicad_pcb file. Throws BoardReadError on failure.
Board ReadBoardFile( const std::filesystem::path& aPath );

/// Parse .kicad_pcb text. Throws BoardReadError on failure.
Board ParseBoard( std::string_view aText );

/// Board files newer than this are KiCad 10 or later. KiCad 9 writes 20241229.
constexpr int MIN_BOARD_VERSION = 20250101;

/// Expand a pad/via layer list entry such as "*.Cu", "F&B.Cu" or "*.Mask".
std::vector<std::string> ExpandLayerWildcard( const std::string& aLayer, int aCopperLayerCount );

} // namespace kirend::pcb
