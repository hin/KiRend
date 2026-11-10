// Loads STEP and VRML models (files or embedded) into triangle meshes with feature edges.
#pragma once

#include "paths/path_resolver.h"
#include "render/mesh.h"

#include <map>
#include <memory>
#include <string>

namespace kirend::models {

struct MeshQuality
{
    double linearDeflectionMm = 0.01;
    double angularDeflectionRad = 0.1;
    double smoothAngleRad = 0.02; ///< B-rep edges between faces within this angle are not drawn
};

/// Load a model. Coordinates are in mm in the model's own frame.
/// Throws std::runtime_error with a readable message on failure.
render::Mesh LoadModel( const ResolvedModel& aModel, const MeshQuality& aQuality );

/// Caches loaded models by resolved path / embedded name.
class ModelCache
{
public:
    explicit ModelCache( const MeshQuality& aQuality ) : m_quality( aQuality ) {}

    /// Returns nullptr (and sets aError) if the model could not be loaded.
    std::shared_ptr<const render::Mesh> get( const ResolvedModel& aModel, std::string& aError );

private:
    MeshQuality                                                   m_quality;
    std::map<std::string, std::shared_ptr<const render::Mesh>>   m_cache;
    std::map<std::string, std::string>                            m_errors;
};

} // namespace kirend::models
