// Top-level render job.
#pragma once

#include "render/camera.h"

#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace kirend {

/// Exit codes, the same values kicad-cli uses.
namespace exit_code {
constexpr int OK = 0;
constexpr int ERR_ARGS = 1;
constexpr int ERR_UNKNOWN = 2;
constexpr int ERR_INVALID_INPUT_FILE = 3;
constexpr int ERR_INVALID_OUTPUT_CONFLICT = 4;
} // namespace exit_code

enum class OutputFormat { Svg, Png };
enum class BackgroundStyle { Default, Transparent, Opaque };

struct RenderJob
{
    std::filesystem::path              input;
    std::filesystem::path              output;          ///< empty: derived from the input name
    OutputFormat                       format = OutputFormat::Png;
    std::map<std::string, std::string> defines;
    render::CameraSettings             camera;
    std::vector<std::string>           layers{ "Edge.Cuts" };
    BackgroundStyle                    background = BackgroundStyle::Default;
    double                             dpi = 300.0;
    bool                               verbose = false;
};

/// Run a render job. Messages go to aLog. Returns an exit code.
int RunRenderJob( const RenderJob& aJob, std::ostream& aLog );

} // namespace kirend
