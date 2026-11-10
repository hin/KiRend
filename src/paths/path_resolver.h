// Resolves 3D model paths the way KiCad's FILENAME_RESOLVER does.
#pragma once

#include "pcb/board.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kirend {

struct ResolvedModel
{
    enum class Kind { NotFound, File, Embedded };

    Kind                      kind = Kind::NotFound;
    std::filesystem::path     path;     ///< Kind::File
    const pcb::EmbeddedFile*  embedded = nullptr; ///< Kind::Embedded
    std::string               displayName;        ///< for messages
    std::string               extension;          ///< lower case, with dot (".step", ".wrl")
};

struct ResolverSettings
{
    std::filesystem::path              boardFile;
    std::map<std::string, std::string> defines;      ///< -D overrides
    /// KiCad user config directory to read kicad_common.json from; empty = default location.
    std::filesystem::path              kicadConfigDir;
};

class PathResolver
{
public:
    explicit PathResolver( const ResolverSettings& aSettings );

    /// Expand ${VAR} and $(VAR) (and %VAR% on Windows). Unknown variables are left as is.
    std::string expandVariables( const std::string& aText ) const;

    /// Value of a variable, using the same precedence as KiCad.
    std::optional<std::string> lookupVariable( const std::string& aName ) const;

    /// Resolve a model path from a footprint. "STEP wins": if a .wrl resolves and a STEP file with
    /// the same name exists next to it (or among the embedded files), the STEP file is returned.
    /// A .wrl that can't be found is replaced by a STEP model with the same name from the search
    /// paths, as kicad-cli does (KiCad 10 libraries no longer ship WRL files).
    ResolvedModel resolveModel( const std::string& aModelPath, const pcb::Footprint& aFootprint,
                                const pcb::Board& aBoard ) const;

    /// Where kicad_common.json was read from (empty if not found).
    const std::filesystem::path& kicadCommonPath() const { return m_kicadCommonPath; }

    /// Text variables found in the project file.
    const std::map<std::string, std::string>& projectVariables() const { return m_projectVars; }

private:
    void loadProjectFile();
    void loadKiCadCommon( const std::filesystem::path& aConfigDir );
    std::optional<std::filesystem::path> findFile( const std::string& aExpanded ) const;
    std::optional<std::filesystem::path> preferStep( const std::filesystem::path& aWrl ) const;
    std::optional<std::filesystem::path> substituteMissingWrl( const std::string& aWrl ) const;
    void                                 buildStepCatalog() const;

    ResolverSettings                   m_settings;
    std::filesystem::path              m_projectDir;
    std::map<std::string, std::string> m_projectVars;
    std::map<std::string, std::string> m_commonVars;   ///< kicad_common.json environment.vars
    std::filesystem::path              m_kicadCommonPath;
    std::string                        m_stock3dModels; ///< built-in default for KICADn_3DMODEL_DIR
    std::vector<std::string>           m_extraSearchDirs; ///< system.extra_3d_search_dirs

    /// STEP files by normalised name, for replacing missing WRL references (built on demand).
    mutable bool                                                m_catalogBuilt = false;
    mutable std::map<std::string, std::vector<std::filesystem::path>> m_stepCatalog;
};

/// Default KiCad user configuration directory (without version), e.g. ~/.config/kicad.
std::filesystem::path DefaultKiCadConfigBase();

/// Newest versioned KiCad config directory (10.0 or later) below aBase, if any.
std::optional<std::filesystem::path> FindKiCadConfigDir( const std::filesystem::path& aBase );

/// Platform default for the stock 3D model library.
std::string DefaultStock3dModelsPath();

} // namespace kirend
