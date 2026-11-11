#pragma once
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "Preset.hpp"
#include "PrintConfig.hpp"
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class PresetBundle;
}
namespace Slic3r::FormorixProfiles {
// Current source copies, taken on the GUI thread. Worker code never reads PresetBundle.
struct Source
{
    Preset printer{Preset::TYPE_PRINTER, ""};
    Preset material{Preset::TYPE_FILAMENT, ""};
    Preset process{Preset::TYPE_PRINT, ""};
    DynamicPrintConfig project;
    DynamicPrintConfig machine; // actual active machine; empty in standalone sources
    std::size_t nozzle_index = 0;
    double layer_height      = 0.; // zero inherits the current Orca process
    std::optional<FormorixProcess::WidthLimits> local_width_limits;
};
struct Resolved
{
    DynamicPrintConfig config;
    std::size_t nozzle_index = 0, material_index = 0;
    FormorixProcess::Profile process;
    std::string source_signature;
};
Source source(const PresetBundle&, const FormorixSurfaces::Head&, double layer_height = 0.);
std::string signature(const Source&);
Resolved resolve(const Source&);
// Clear before attempting resolution: a missing/invalid source cannot leave a usable stale snapshot.
Resolved refresh(const PresetBundle&, FormorixSurfaces::Head&);
// GUI refresh hook. An empty cache also invalidates snapshots loaded from a project.
bool invalidate_changed(FormorixSurfaces::Project&, const PresetBundle&, std::array<std::string, 3>& source_signatures);
// Add portable full copies of current head bindings, including inactive sources.
// Replace older copies with the same type/name. Original presets are untouched;
// resolution failures leave the owning output vector unchanged.
void append_project_sources(const PresetBundle&, const FormorixSurfaces::Project&, std::vector<std::unique_ptr<Preset>>&);
// Before native import, reject an explicit archived head source that would be
// shadowed by a differing same-name local source. Does not mutate either input.
void validate_project_sources(const PresetBundle&, const FormorixSurfaces::Project&, const std::vector<Preset*>&);
} // namespace Slic3r::FormorixProfiles
