// Style sheets: line widths, colours and what is drawn, read from YAML.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kirend::style {

struct Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    std::string hex() const; ///< "#rrggbb"

    bool operator==( const Color& ) const = default;
};

struct Stroke
{
    double widthPt = 0.36;
    Color  color;

    bool operator==( const Stroke& ) const = default;
};

/// Kinds of lines that can be styled separately.
enum class LineCategory : uint8_t { Board, Models, Pads, Tracks, Vias, Graphics };

constexpr size_t LINE_CATEGORY_COUNT = 6;

/// Name used in style sheets and SVG group ids ("board", "models", ...).
const char* CategoryName( LineCategory aCategory );

/// Strokes for one line category. Board layer categories only use `edge`.
struct CategoryStrokes
{
    Stroke outline;
    Stroke edge;

    bool operator==( const CategoryStrokes& ) const = default;
};

struct Visibility
{
    std::vector<std::string> layers;
    bool                     models = true;
    bool                     dnp = false;
    bool                     pads = true;
    bool                     tracks = true;
    bool                     vias = true;
    bool                     graphics = true;

    bool operator==( const Visibility& ) const = default;
};

struct Style
{
    std::optional<Color>                             background; ///< nullopt: transparent
    Visibility                                       visible;
    std::array<CategoryStrokes, LINE_CATEGORY_COUNT> lines;

    const CategoryStrokes& strokes( LineCategory aCategory ) const { return lines[static_cast<size_t>( aCategory )]; }
    CategoryStrokes&       strokes( LineCategory aCategory ) { return lines[static_cast<size_t>( aCategory )]; }

    bool operator==( const Style& ) const = default;
};

/// Error in a style sheet; the message includes the file name, line and column.
class StyleError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Text of the built-in default style sheet (styles/default.yaml, compiled in).
const std::string& DefaultStyleText();

/// The built-in default style.
const Style& DefaultStyle();

/// Apply the settings in aText on top of aStyle. Settings that aren't in aText keep their value.
/// aSourceName is used in error messages. Throws StyleError.
void ApplyStyle( const std::string& aText, const std::string& aSourceName, Style& aStyle );

/// The default style with aFile applied on top. Throws StyleError.
Style LoadStyleFile( const std::filesystem::path& aFile );

/// Convert a width in points to pixels at the given DPI.
inline double PointsToPixels( double aPoints, double aDpi ) { return aPoints * aDpi / 72.0; }

} // namespace kirend::style
