#pragma once

#include "kirend.h"

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace kirend::cli {

struct ParseResult
{
    std::optional<RenderJob> job;      ///< set when a render should run
    int                      exitCode = exit_code::OK;
};

/// Parse command line arguments. Help, version and errors are written to aOut / aErr.
ParseResult ParseCommandLine( const std::vector<std::string>& aArgs, std::ostream& aOut, std::ostream& aErr );

/// Parse "X,Y,Z" (optionally quoted with single quotes), like kicad-cli.
std::optional<Vec3> ParseVector3( const std::string& aText );

} // namespace kirend::cli
