// Triangle meshes and lines in world space (mm, X right, Y up, Z out of the board top).
#pragma once

#include "geometry/vec.h"
#include "style/style.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kirend::render {

struct Mesh
{
    std::vector<Vec3>                    vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    /// Edges that are always drawn when visible (sharp B-rep edges, creases), as vertex pairs.
    std::vector<std::array<uint32_t, 2>> featureEdges;
    /// How the mesh's lines are styled.
    style::LineCategory                  category = style::LineCategory::Models;

    void append( const Mesh& aOther );
};

/// Lines that lie on surfaces but don't occlude anything (pad and track outlines, silkscreen).
struct LineSet
{
    std::vector<std::vector<Vec3>> polylines;
    style::LineCategory            category = style::LineCategory::Graphics;
};

struct Scene
{
    std::vector<Mesh>    meshes;
    std::vector<LineSet> lines;
};

} // namespace kirend::render
