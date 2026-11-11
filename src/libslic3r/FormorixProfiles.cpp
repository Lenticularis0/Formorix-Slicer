#include "FormorixProfiles.hpp"
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "Preset.hpp"
#include "PresetBundle.hpp"
#include "PrintConfig.hpp"
#include "Config.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::FormorixProfiles {
namespace {
const Preset& current(const PresetCollection& collection, const std::string& name)
{
    const auto& edited = collection.get_edited_preset();
    if (name.empty() || name == edited.name)
        return edited;
    const auto* saved = collection.find_preset(name, false);
    if (!saved)
        throw std::invalid_argument("An assigned Formorix material or nozzle preset is missing");
    return *saved;
}
nlohmann::json contents(const DynamicPrintConfig& config)
{
    nlohmann::json result = nlohmann::json::object();
    for (const auto& key : config.keys())
        result[key] = config.opt_serialize(key);
    return result;
}
nlohmann::json contents(const Preset& preset)
{
    return {{"name", preset.name},
            {"setting_id", preset.setting_id},
            {"filament_id", preset.filament_id},
            {"type", int(preset.type)},
            {"config", contents(preset.config)}};
}
} // namespace
Source source(const PresetBundle& bundle, const FormorixSurfaces::Head& head, double layer_height)
{
    Source s;
    s.printer  = current(bundle.printers, head.printer_preset);
    s.material = current(bundle.filaments, head.material_preset);
    s.process  = bundle.prints.get_edited_preset();
    s.project  = bundle.project_config;
    s.machine  = bundle.printers.get_edited_preset().config;
    // A separately assigned nozzle source has its own volume type; the active
    // machine's project selector belongs to its own nozzles, not this preset.
    if (s.printer.name != bundle.printers.get_edited_preset().name)
        if (const auto* type = s.printer.config.option("default_nozzle_volume_type"))
            s.project.set_key_value("nozzle_volume_type", type->clone());
    s.nozzle_index = head.nozzle_index;
    s.layer_height = layer_height;
    return s;
}
std::string signature(const Source& s)
{
    // Exact serialized contents, not a name, timestamp or collision-prone hash.
    return nlohmann::json{{"printer", contents(s.printer)}, {"material", contents(s.material)}, {"process", contents(s.process)},
                          {"project", contents(s.project)}, {"machine", contents(s.machine)},   {"nozzle_index", s.nozzle_index},
                          {"layer_height", s.layer_height}}
        .dump();
}
Resolved resolve(const Source& source)
{
    if (source.printer.type != Preset::TYPE_PRINTER || source.material.type != Preset::TYPE_FILAMENT ||
        source.process.type != Preset::TYPE_PRINT || source.printer.name.empty() || source.material.name.empty() ||
        !std::isfinite(source.layer_height) || source.layer_height < 0.)
        throw std::invalid_argument("Invalid Formorix profile source");
    const auto* nozzle = source.printer.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!nozzle || source.nozzle_index >= nozzle->values.size())
        throw std::invalid_argument("The assigned Formorix nozzle slot does not exist");
    Preset printer = source.printer, process = source.process;
    // A nozzle binding must not replace the active machine's bed or experimental guard.
    if (!source.machine.empty()) {
        printer.config = source.machine;
        for (const auto& key : print_config_def.extruder_option_keys())
            if (const auto* option = source.printer.config.option(key))
                printer.config.set_key_value(key, option->clone());
        for (const char* key : {"extruder_variant_list", "printer_extruder_id", "printer_extruder_variant", "extruder_nozzle_stats"})
            if (const auto* option = source.printer.config.option(key))
                printer.config.set_key_value(key, option->clone());
    }
    auto project = source.project;
    if (!project.has("nozzle_volume_type"))
        if (const auto* type = source.printer.config.option("default_nozzle_volume_type"))
            project.set_key_value("nozzle_volume_type", type->clone());
    const auto* volume_types   = project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    const auto* extruder_types = printer.config.option<ConfigOptionEnumsGeneric>("extruder_type");
    if (!volume_types || volume_types->values.empty() || !extruder_types || extruder_types->values.empty())
        throw std::invalid_argument("Missing nozzle volume or extruder type in Formorix source");
    std::vector<std::vector<NozzleVolumeType>> inventory;
    printer.config.get_extruder_nozzle_volume_count(int(nozzle->size()), inventory);
    for (size_t i = 0; i < nozzle->size(); ++i)
        if (volume_types->get_at(i) == nvtHybrid || inventory[i].size() > 1)
            throw std::invalid_argument(
                "Formorix requires one concrete nozzle per assigned extruder; mixed nozzle inventories are not supported");
    const auto volume_type   = NozzleVolumeType(volume_types->get_at(source.nozzle_index));
    const auto extruder_type = ExtruderType(extruder_types->get_at(source.nozzle_index));
    if (source.material.config.get_index_for_extruder(1, "", extruder_type, volume_type, "filament_extruder_variant") < 0)
        throw std::invalid_argument("The assigned material has no matching variant for this nozzle type");
    std::vector<Preset> materials{source.material};
    Resolved out;
    out.config = PresetBundle::construct_full_config(printer, process, project, materials, true,
                                                     std::vector<int>{int(source.nozzle_index + 1)}, std::vector<int>{int(volume_type)});
    out.nozzle_index             = source.nozzle_index;
    out.source_signature         = signature(source);
    const auto* resolved_nozzles = out.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!resolved_nozzles || out.nozzle_index >= resolved_nozzles->values.size() ||
        resolved_nozzles->values[out.nozzle_index] != nozzle->values[source.nozzle_index])
        throw std::invalid_argument("The nozzle preset layout could not be resolved without changing its slot");
    const double diameter = resolved_nozzles->values[out.nozzle_index];
    double width          = out.config.get_abs_value("outer_wall_line_width", diameter);
    if (width == 0.)
        width = out.config.get_abs_value("line_width", diameter);
    if (width == 0.)
        width = diameter * 1.05;
    const double height = source.layer_height > 0. ? source.layer_height : out.config.opt_float("layer_height");
    const auto* speeds  = dynamic_cast<const ConfigOptionVector<double>*>(out.config.option("outer_wall_speed"));
    if (!speeds || speeds->values.empty())
        throw std::invalid_argument("Missing Formorix surface speed");
    const double speed            = speeds->get_at(out.nozzle_index);
    const std::string material_id = source.material.filament_id.empty() || source.material.filament_id == UNKNOWN_FILAMENT_ID ?
                                        source.material.name :
                                        source.material.filament_id;
    out.process = FormorixProcess::from_orca(out.config, out.nozzle_index, 0, material_id,
                                             source.printer.name + " / " + std::to_string(out.nozzle_index), width, height, speed);
    return out;
}
Resolved refresh(const PresetBundle& bundle, FormorixSurfaces::Head& head)
{
    head.process.reset();
    auto result  = resolve(source(bundle, head));
    head.process = result.process;
    return result;
}
bool invalidate_changed(FormorixSurfaces::Project& project, const PresetBundle& bundle, std::array<std::string, 3>& signatures)
{
    bool changed = false;
    for (std::size_t i = 0; i < project.heads.size(); ++i) {
        auto& head = project.heads[i];
        std::string current_signature;
        try {
            current_signature = signature(source(bundle, head));
        } catch (const std::exception&) {
            current_signature = "missing";
            if (head.process) {
                head.process.reset();
                changed = true;
            }
        }
        if (current_signature != signatures[i]) {
            if (head.process) {
                head.process.reset();
                changed = true;
            }
            signatures[i] = std::move(current_signature);
        }
    }
    return changed;
}
} // namespace Slic3r::FormorixProfiles
