// In-memory model of the parts of a .kicad_pcb file that KiRend draws.
// All lengths are in millimetres, in KiCad board coordinates (X right, Y down).
#pragma once

#include "geometry/vec.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kirend::pcb {

/// A graphic primitive: board drawing, footprint graphic or custom pad primitive.
struct Shape
{
    enum class Kind { Segment, Arc, Circle, Rect, Poly, Bezier };

    Kind              kind = Kind::Segment;
    std::string       layer;
    double            width = 0.0;  ///< Stroke width; 0 for none.
    bool              filled = false;
    Vec2              start;        ///< Segment/Arc/Rect start, Circle centre, Bezier p0.
    Vec2              mid;          ///< Arc mid point.
    Vec2              end;          ///< Segment/Arc/Rect end, Circle point on circumference.
    Vec2              ctrl1, ctrl2; ///< Bezier control points (end = p3).
    double            rectAngle = 0.0; ///< Rotation of a rectangle around its start? (unused)
    double            cornerRadius = 0.0; ///< Rounded rectangle radius.
    /// Polygon outline. Arcs inside polygons are already approximated with points.
    std::vector<Vec2> points;
};

struct Drill
{
    bool   oval = false;
    double sizeX = 0.0;   ///< Diameter, or oval width.
    double sizeY = 0.0;   ///< Oval height (equals sizeX for round holes).
    Vec2   offset;        ///< Offset of the pad shape relative to the hole.

    bool present() const { return sizeX > 0.0; }
};

struct Pad
{
    enum class Type { ThroughHole, Smd, Connect, NpThroughHole };
    enum class ShapeKind { Circle, Rect, Oval, Trapezoid, RoundRect, Custom };

    std::string              number;
    Type                     type = Type::Smd;
    ShapeKind                shape = ShapeKind::Rect;
    Vec2                     position;      ///< Absolute board position.
    double                   angle = 0.0;   ///< Absolute orientation in degrees.
    Vec2                     size;
    Vec2                     rectDelta;     ///< Trapezoid delta.
    double                   roundRectRatio = 0.0;
    double                   chamferRatio = 0.0;
    int                      chamferCorners = 0; ///< bit 0 TL, 1 TR, 2 BL, 3 BR
    Drill                    drill;
    std::vector<std::string> layers;        ///< Explicit layers, wildcards expanded.
    std::vector<Shape>       primitives;    ///< Custom pad primitives, pad-local coordinates.
    bool                     customAnchorCircle = true;
};

struct Model3D
{
    std::string path;
    Vec3        offset;            ///< mm
    Vec3        scale{ 1, 1, 1 };
    Vec3        rotation;          ///< degrees
    bool        hidden = false;
    double      opacity = 1.0;
};

struct EmbeddedFile
{
    std::string name;
    std::string type;
    std::string data;     ///< Raw (still base64 + zstd encoded) data block.
    std::string checksum;
};

struct Footprint
{
    std::string                         libId;       ///< "Lib:Name"
    std::string                         reference;
    Vec2                                position;
    double                              angle = 0.0; ///< degrees
    bool                                flipped = false;
    bool                                smd = false;
    bool                                throughHole = false;
    bool                                boardOnly = false;
    bool                                excludeFromPos = false;
    bool                                dnp = false;
    std::vector<Shape>                  graphics;    ///< Absolute board coordinates.
    std::vector<Pad>                    pads;
    std::vector<Model3D>                models;
    std::vector<EmbeddedFile>           embeddedFiles;
};

struct Track
{
    bool        isArc = false;
    Vec2        start;
    Vec2        mid;
    Vec2        end;
    double      width = 0.0;
    std::string layer;
};

struct Via
{
    Vec2                     position;
    double                   size = 0.0;
    double                   drill = 0.0;
    std::vector<std::string> layers;
};

struct StackupInfo
{
    bool   present = false;
    double dielectricThickness = 0.0; ///< Sum of dielectric layers (+ inner copper), mm.
    double frontCopper = 0.035;
    double backCopper = 0.035;
    double frontMask = 0.025;
    double backMask = 0.025;
};

struct Board
{
    std::string                  fileName;
    int                          version = 0;
    double                       generalThickness = 1.6;
    int                          copperLayerCount = 2;
    std::map<std::string, std::string> layerAliases; ///< user name -> canonical name
    StackupInfo                  stackup;
    std::vector<Shape>           drawings;
    std::vector<Footprint>       footprints;
    std::vector<Track>           tracks;
    std::vector<Via>             vias;
    std::vector<EmbeddedFile>    embeddedFiles;
    std::vector<std::string>     warnings; ///< problems found while reading
};

} // namespace kirend::pcb
