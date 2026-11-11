#include "FormorixProfiles.hpp"
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "Preset.hpp"
#include "PresetBundle.hpp"
#include "PrintConfig.hpp"
#include "Config.hpp"
#include "Flow.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r::FormorixProfiles {
namespace {
const Preset& current(const PresetCollection& collection, const std::string& name)
{
    const auto& edited = collection.get_edited_preset();
    if (name.empty() || name == edited.name)
        return edited;
    const auto* saved = collection.find_preset(name, false);
    if (!saved)
        throw std::invalid_argument(L("An assigned Formorix material, nozzle or process preset is missing"));
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
    s.process  = current(bundle.prints, head.process_preset);
    s.project  = bundle.project_config;
    s.machine  = bundle.printers.get_edited_preset().config;
    // A separately assigned nozzle source has its own volume type; the active
    // machine's project selector belongs to its own nozzles, not this preset.
    if (s.printer.name != bundle.printers.get_edited_preset().name)
        if (const auto* type = s.printer.config.option("default_nozzle_volume_type"))
            s.project.set_key_value("nozzle_volume_type", type->clone());
    s.nozzle_index = head.nozzle_index;
    s.layer_height = layer_height;
    s.local_width_limits = head.local_width_limits;
    return s;
}
std::string signature(const Source& s)
{
    // Exact serialized contents, not a name, timestamp or collision-prone hash.
    nlohmann::json result{{"printer", contents(s.printer)}, {"material", contents(s.material)}, {"process", contents(s.process)},
                          {"project", contents(s.project)}, {"machine", contents(s.machine)},   {"nozzle_index", s.nozzle_index},
                          {"layer_height", s.layer_height}};
    if (s.local_width_limits) result["local_width_limits"] = *s.local_width_limits;
    return result.dump();
}
Resolved resolve(const Source& source)
{
    if (source.printer.type != Preset::TYPE_PRINTER || source.material.type != Preset::TYPE_FILAMENT ||
        source.process.type != Preset::TYPE_PRINT || source.printer.name.empty() || source.material.name.empty() ||
        !std::isfinite(source.layer_height) || source.layer_height < 0. ||
        (source.local_width_limits && !FormorixProcess::valid(*source.local_width_limits)))
        throw std::invalid_argument(_u8L("Invalid Formorix profile source"));
    const auto* nozzle = source.printer.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!nozzle || source.nozzle_index >= nozzle->values.size())
        throw std::invalid_argument(_u8L("The assigned Formorix nozzle slot does not exist"));
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
        throw std::invalid_argument(_u8L("Missing nozzle volume or extruder type in Formorix source"));
    std::vector<std::vector<NozzleVolumeType>> inventory;
    printer.config.get_extruder_nozzle_volume_count(int(nozzle->size()), inventory);
    for (size_t i = 0; i < nozzle->size(); ++i)
        if (volume_types->get_at(i) == nvtHybrid || inventory[i].size() > 1)
            throw std::invalid_argument(
                _u8L("Formorix requires one concrete nozzle per assigned extruder; mixed nozzle inventories are not supported"));
    const auto volume_type   = NozzleVolumeType(volume_types->get_at(source.nozzle_index));
    const auto extruder_type = ExtruderType(extruder_types->get_at(source.nozzle_index));
    if (source.material.config.get_index_for_extruder(1, "", extruder_type, volume_type, "filament_extruder_variant") < 0)
        throw std::invalid_argument(_u8L("The assigned material has no matching variant for this nozzle type"));
    std::vector<Preset> materials{source.material};
    Resolved out;
    out.config = PresetBundle::construct_full_config(printer, process, project, materials, true,
                                                     std::vector<int>{int(source.nozzle_index + 1)}, std::vector<int>{int(volume_type)});
    out.nozzle_index             = source.nozzle_index;
    out.source_signature         = signature(source);
    const auto* resolved_nozzles = out.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!resolved_nozzles || out.nozzle_index >= resolved_nozzles->values.size() ||
        resolved_nozzles->values[out.nozzle_index] != nozzle->values[source.nozzle_index])
        throw std::invalid_argument(_u8L("The nozzle preset layout could not be resolved without changing its slot"));
    const double diameter = resolved_nozzles->values[out.nozzle_index];
    const double height = source.layer_height > 0. ? source.layer_height : out.config.opt_float("layer_height");
    const auto* width_option = out.config.option<ConfigOptionFloatOrPercent>("outer_wall_line_width");
    if (width_option && width_option->value == 0.)
        width_option = out.config.option<ConfigOptionFloatOrPercent>("line_width");
    if (!width_option) throw std::invalid_argument(L("Missing Orca surface line width"));
    const auto native_flow = Flow::new_from_config_width(frExternalPerimeter, *width_option, float(diameter), float(height));
    // Preserve explicitly configured dimensions in double precision; only the
    // existing native automatic-width rule has a float-valued result.
    const double width = !width_option->percent && width_option->value <= 0. ?
                             double(native_flow.width()) : width_option->get_abs_value(diameter);
    const auto* speeds  = dynamic_cast<const ConfigOptionVector<double>*>(out.config.option("outer_wall_speed"));
    if (!speeds || speeds->values.empty())
        throw std::invalid_argument(_u8L("Missing Formorix surface speed"));
    const double speed            = speeds->get_at(out.nozzle_index);
    const std::string material_id = source.material.filament_id.empty() || source.material.filament_id == UNKNOWN_FILAMENT_ID ?
                                        source.material.name :
                                        source.material.filament_id;
    out.process = FormorixProcess::from_orca(out.config, out.nozzle_index, 0, material_id,
                                             source.printer.name + " / " + std::to_string(out.nozzle_index), width, height, speed);
    out.process.local_width_limits = source.local_width_limits;
    if (!FormorixProcess::valid(out.process))
        throw std::invalid_argument(L("The Orca surface width is outside the declared local line width limits"));
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
void append_project_sources(const PresetBundle& bundle,
                            const FormorixSurfaces::Project& project,
                            std::vector<std::unique_ptr<Preset>>& output)
{
    std::vector<std::unique_ptr<Preset>> staged;
    const auto add = [&](const PresetCollection& collection, const std::string& name) {
        const auto& selected = current(collection, name);
        const auto& base     = collection.default_preset_for(selected.config);
        if (selected.is_default) {
            // Implicit bindings follow the active full project configuration.
            // An explicit edited Default cannot independently inherit itself.
            if (!name.empty() && !selected.config.diff(base.config).empty())
                throw std::invalid_argument(_u8L("Save the edited default Formorix source as a named preset before saving the project"));
            return;
        }
        if (std::any_of(staged.begin(), staged.end(),
                        [&](const auto& preset) { return preset->type == selected.type && preset->name == selected.name; }))
            return;
        auto copy = std::make_unique<Preset>(selected);
        // A full copied configuration must not depend on an unavailable vendor
        // or user-parent chain. Native import accepts its own typed Default base.
        copy->config.set_key_value("inherits", new ConfigOptionString(base.name));
        // Match PresetBundle::full_config_secure(): source machine credentials
        // are not part of portable material/process geometry.
        for (const char* key : {"print_host", "print_host_webui", "printhost_apikey", "printhost_cafile", "printhost_user",
                                "printhost_password", "printhost_port"})
            copy->config.erase(key);
        if (copy->type == Preset::TYPE_FILAMENT)
            copy->config.set_key_value("filament_settings_id", new ConfigOptionStrings{copy->name});
        else
            copy->config.set_key_value(copy->type == Preset::TYPE_PRINTER ? "printer_settings_id" : "print_settings_id",
                                       new ConfigOptionString(copy->name));
        copy->is_system = copy->is_default = false;
        copy->is_external = copy->is_project_embedded = true;
        staged.push_back(std::move(copy));
    };
    for (const auto& head : project.heads) {
        add(bundle.printers, head.printer_preset);
        add(bundle.filaments, head.material_preset);
        add(bundle.prints, head.process_preset);
    }
    // All lookups and allocations finish before changing the existing owners.
    output.reserve(output.size() + staged.size());
    for (auto& copy : staged) {
        const auto previous = std::find_if(output.begin(), output.end(), [&](const auto& preset) {
            return preset && preset->type == copy->type && preset->name == copy->name;
        });
        if (previous == output.end())
            output.push_back(std::move(copy));
        else
            *previous = std::move(copy);
    }
}
void validate_project_sources(const PresetBundle& bundle, const FormorixSurfaces::Project& project, const std::vector<Preset*>& imported)
{
    const auto check = [&](const PresetCollection& collection, Preset::Type type, const std::string& name) {
        if (name.empty())
            return; // implicit bindings follow the active full project config
        const auto archived = std::find_if(imported.begin(), imported.end(),
                                           [&](const auto* preset) { return preset && preset->type == type && preset->name == name; });
        if (archived == imported.end())
            return; // an old/external-only reference carries no archived override to compare
        const auto& edited  = collection.get_edited_preset();
        const auto* local   = edited.name == name ? &edited : collection.find_preset(name, false);
        const auto conflict = [&] {
            throw std::invalid_argument(format(_u8L("The Formorix project source \"%1%\" has conflicting definitions. Import or rename the "
                                                    "source explicitly before loading this project."),
                                               name));
        };
        if (std::count_if(imported.begin(), imported.end(),
                          [&](const auto* preset) { return preset && preset->type == type && preset->name == name; }) != 1)
            conflict();
        if (!local)
            return;
        if (type == Preset::TYPE_FILAMENT && !(*archived)->filament_id.empty() && (*archived)->filament_id != UNKNOWN_FILAMENT_ID &&
            (*archived)->filament_id != local->filament_id)
            conflict();
        for (const auto& key : (*archived)->config.keys()) {
            // Compare every explicitly archived source value. Full portable
            // copies and legacy inherited diffs both retain their actual inputs.
            if (key == "inherits" || key == "printer_settings_id" || key == "filament_settings_id" || key == "print_settings_id" ||
                key == "thumbnails" || // native import inserts separator spaces in this image-preview-only specification
                key == "print_host" || key == "print_host_webui" || key == "printhost_apikey" || key == "printhost_cafile" ||
                key == "printhost_user" || key == "printhost_password" || key == "printhost_port")
                continue;
            const auto* current_value = local->config.option(key);
            if (key == "machine_max_junction_deviation") {
                // Native import expands this machine-limit vector to normal/
                // silent mode. A missing second value uses get_at(1)'s first-
                // value fallback; compare those actual values, not the length.
                const auto* local_modes    = dynamic_cast<const ConfigOptionFloats*>(current_value);
                const auto* archived_modes = dynamic_cast<const ConfigOptionFloats*>((*archived)->config.option(key));
                if (local_modes && archived_modes && !local_modes->values.empty() && local_modes->values.size() <= 2 &&
                    !archived_modes->values.empty() && archived_modes->values.size() <= 2) {
                    const ConfigOptionFloats normalized_local{local_modes->get_at(0), local_modes->get_at(1)};
                    const ConfigOptionFloats normalized_archive{archived_modes->get_at(0), archived_modes->get_at(1)};
                    if (normalized_local.serialize() == normalized_archive.serialize())
                        continue;
                }
            }
            if (!current_value || current_value->serialize() != (*archived)->config.option(key)->serialize())
                conflict();
        }
    };
    for (const auto& head : project.heads) {
        check(bundle.printers, Preset::TYPE_PRINTER, head.printer_preset);
        check(bundle.filaments, Preset::TYPE_FILAMENT, head.material_preset);
        check(bundle.prints, Preset::TYPE_PRINT, head.process_preset);
    }
}
} // namespace Slic3r::FormorixProfiles
