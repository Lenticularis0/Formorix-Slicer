#include "FormorixCorePreview.hpp"
#include "FormorixGeometry.hpp"
#include "FormorixProcess.hpp"
#include "PrintConfig.hpp"
#include "Point.hpp"
#include "Config.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Model.hpp"
#include "Polyline.hpp"
#include "Print.hpp"
#include "PrintBase.hpp"
#include "TriangleMesh.hpp"
#include "libslic3r.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

namespace Slic3r::FormorixCorePreview {
namespace {
void project_options(DynamicPrintConfig& config, const std::vector<std::string>& keys, std::size_t index)
{
    for (const auto& key : keys) {
        auto* value = dynamic_cast<ConfigOptionVectorBase*>(config.option(key));
        if (!value || value->size() == 0)
            continue;
        // A single slot is an inherited per-printer default, not another nozzle's data.
        if (index >= value->size() && value->size() != 1)
            throw std::invalid_argument("Missing explicit per-head Orca option: " + key);
        std::unique_ptr<ConfigOption> original(value->clone());
        value->resize(1);
        value->set_at(original.get(), 0,
                      original->is_vector() && index < static_cast<ConfigOptionVectorBase*>(original.get())->size() ? index : 0);
    }
}
DynamicPrintConfig configuration(const Source& source)
{
    if (!FormorixProcess::valid(source.process))
        throw std::invalid_argument("Invalid resolved head profile");
    auto config = DynamicPrintConfig::full_print_config();
    config.apply(source.config);
    if (config.opt_bool("belt_printer") || config.opt_bool("spiral_mode"))
        throw std::invalid_argument("Hybrid core preview does not support belt or spiral mode");
    project_options(config, print_config_def.extruder_option_keys(), source.nozzle_index);
    project_options(config, print_config_def.filament_option_keys(), source.material_index);
    project_options(config, std::vector<std::string>(print_options_with_variant.begin(), print_options_with_variant.end()),
                    source.nozzle_index);
    project_options(config, std::vector<std::string>(filament_options_with_variant.begin(), filament_options_with_variant.end()),
                    source.material_index);
    project_options(config, std::vector<std::string>(printer_options_with_variant_1.begin(), printer_options_with_variant_1.end()),
                    source.nozzle_index);
    project_options(config, {"nozzle_volume_type", "extruder_bed_exclude_volumes"}, source.nozzle_index);
    // Machine limits contain normal/silent pairs for each physical nozzle.
    for (const auto& key : printer_options_with_variant_2) {
        auto* value = dynamic_cast<ConfigOptionVectorBase*>(config.option(key));
        if (!value || value->size() == 0)
            continue;
        const std::size_t first = value->size() <= 2 ? 0 : 2 * source.nozzle_index;
        if (value->size() != 1 && first + 1 >= value->size())
            throw std::invalid_argument("Missing paired machine limit: " + key);
        std::unique_ptr<ConfigOption> original(value->clone());
        const std::size_t second = value->size() == 1 ? 0 : first + 1;
        value->resize(2);
        value->set_at(original.get(), 0, first);
        value->set_at(original.get(), 1, second);
    }
    const auto type   = ExtruderType(config.option<ConfigOptionEnumsGeneric>("extruder_type")->get_at(0));
    const auto volume = NozzleVolumeType(config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->get_at(0));
    if (volume == nvtHybrid)
        throw std::invalid_argument("Hybrid core preview requires one concrete nozzle volume type per head");
    const auto variant = get_extruder_variant_string(type, volume);
    for (const char* key : {"extruder_variant_list", "printer_extruder_variant", "print_extruder_variant", "filament_extruder_variant"})
        config.set_key_value(key, new ConfigOptionStrings{variant});
    for (const char* key : {"printer_extruder_id", "print_extruder_id", "filament_self_index", "filament_map", "filament_nozzle_map"})
        config.set_key_value(key, new ConfigOptionInts{1});
    config.set_key_value("filament_volume_map", new ConfigOptionInts{int(volume)});
    config.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings{});
    // Orca's filament list intentionally omits a few composite-preset fields.
    project_options(config,
                    {"filament_type", "filament_vendor", "filament_start_gcode", "filament_change_length", "filament_change_length_nc",
                     "filament_ramming_travel_time", "filament_ramming_travel_time_nc", "filament_ramming_volumetric_speed",
                     "filament_ramming_volumetric_speed_nc", "nozzle_temperature", "nozzle_temperature_initial_layer", "filament_shrink",
                     "filament_shrinkage_compensation_z"},
                    source.material_index);
    const auto& p = source.process;
    for (double factor : p.shrink_compensation)
        if (std::abs(factor - 1.) > 1e-12)
            throw std::invalid_argument(
                "Hybrid core preview requires neutral XY/Z shrink compensation until core and shell share a compensation transform.");
    for (const char* key : {"filament_shrink", "filament_shrinkage_compensation_z"}) {
        const auto* values = config.option<ConfigOptionPercents>(key);
        if (!values || values->values.empty() || !std::isfinite(values->get_at(0)) || std::abs(values->get_at(0) - 100.) > 1e-10)
            throw std::invalid_argument(std::string("Hybrid core preview requires 100% ") + key +
                                        "; automatic shrink neutralization is not applied.");
    }
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats{p.nozzle_diameter});
    config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{p.flow_ratio});
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{p.volumetric_limit});
    config.set_key_value("nozzle_temperature", new ConfigOptionInts{int(std::lround(p.temperature))});
    config.set_key_value("layer_height", new ConfigOptionFloat(p.layer_height));
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats{0.});
    config.set_deserialize_strict("enable_prime_tower", "0");
    for (const char* key : {"outer_wall_filament_id", "inner_wall_filament_id", "sparse_infill_filament_id", "internal_solid_filament_id",
                            "top_surface_filament_id", "bottom_surface_filament_id"})
        config.set_deserialize_strict(key, "1");
    config.set_deserialize_strict("support_filament", "1");
    config.set_deserialize_strict("support_interface_filament", "1");
    // This remains explicitly experimental; no "temporarily disable safety" config.
    config.set_deserialize_strict("formorix_5d_enabled", "1");
    return config;
}
const char* speed_key(ExtrusionRole role)
{
    switch (role) {
    case erExternalPerimeter: return "outer_wall_speed";
    case erPerimeter: return "inner_wall_speed";
    case erInternalInfill: return "sparse_infill_speed";
    case erSolidInfill: return "internal_solid_infill_speed";
    case erTopSolidInfill:
    case erBottomSurface: return "top_surface_speed";
    case erBridgeInfill:
    case erOverhangPerimeter:
    case erInternalBridgeInfill: return "bridge_speed";
    case erGapFill: return "gap_infill_speed";
    case erIroning: return "ironing_speed";
    case erSupportMaterialInterface: return "support_interface_speed";
    default: return "support_speed";
    }
}
double nominal_speed(const DynamicPrintConfig& config, ExtrusionRole role, double fallback)
{
    const auto* option = config.option(speed_key(role));
    if (!option)
        return fallback;
    std::string value = option->serialize();
    if (auto* vector = dynamic_cast<const ConfigOptionVectorBase*>(option)) {
        if (vector->size() == 0)
            return fallback;
        value = vector->vserialize().front();
    }
    try {
        std::size_t end = 0;
        double result   = std::stod(value, &end);
        if (end < value.size() && value[end] == '%')
            result *= fallback / 100.;
        return std::isfinite(result) && result > 0. ? result : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}
double native_role_flow(const DynamicPrintConfig& config, ExtrusionRole role)
{
    // Match the static role factors in GCode::_extrude. Bridge geometry and
    // ironing flow already contribute to ExtrusionPath::mm3_per_mm.
    switch (role) {
    case erTopSolidInfill: return config.opt_float("top_solid_infill_flow_ratio", 0);
    case erBottomSurface: return config.opt_float("bottom_solid_infill_flow_ratio");
    case erInternalBridgeInfill: return config.opt_float("internal_bridge_flow");
    case erBrim: return config.opt_float("brim_flow_ratio");
    default: break;
    }
    if (config.opt_bool("set_other_flow_ratios")) {
        const char* key = nullptr;
        switch (role) {
        case erExternalPerimeter: key = "outer_wall_flow_ratio"; break;
        case erPerimeter: key = "inner_wall_flow_ratio"; break;
        case erOverhangPerimeter: key = "overhang_flow_ratio"; break;
        case erInternalInfill: key = "sparse_infill_flow_ratio"; break;
        case erSolidInfill: key = "internal_solid_infill_flow_ratio"; break;
        case erGapFill: key = "gap_fill_flow_ratio"; break;
        case erSupportMaterial: key = "support_flow_ratio"; break;
        case erSupportMaterialInterface: key = "support_interface_flow_ratio"; break;
        default: break; // SupportTransition, bridges and ironing have no extra role factor here.
        }
        if (key)
            return config.opt_float(key);
    }
    return 1.;
}
void append_entity(Result& result,
                   const ExtrusionEntity& entity,
                   double z,
                   const Point& shift,
                   const Vec3d& origin,
                   int head,
                   const Source& source,
                   const DynamicPrintConfig& config,
                   bool support,
                   bool first_layer)
{
    if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
        for (const auto* child : collection->entities)
            append_entity(result, *child, z, shift, origin, head, source, config, support, first_layer);
    } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        for (const auto& path : loop->paths)
            append_entity(result, path, z, shift, origin, head, source, config, support, first_layer);
    } else if (const auto* multi = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
        for (const auto& path : multi->paths)
            append_entity(result, path, z, shift, origin, head, source, config, support, first_layer);
    } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        if (path->is_force_no_extrusion() || path->polyline.points.size() < 2)
            return;
        if (dynamic_cast<const ExtrusionPathSloped*>(path) || path->z_contoured)
            throw std::invalid_argument("Sloped or contoured conventional extrusions are not supported by hybrid preview");
        Path out;
        out.head                 = head;
        out.role                 = path->role();
        out.support              = support;
        out.width                = path->width;
        out.height               = path->height;
        out.mm3_per_mm           = path->mm3_per_mm;
        out.process              = source.process;
        // Explicit adaptive 5D limits do not constrain Orca's native role widths.
        out.process.local_width_limits.reset();
        out.process.surface_flow_ratio = 1.; // The resolved 5D outer-wall factor is not a native role factor.
        out.process.print_flow_ratio = config.opt_float("print_flow_ratio");
        out.process.flow_ratio = config.opt_float("filament_flow_ratio", 0);
        out.role_flow_ratio = native_role_flow(config, out.role);
        if (first_layer && config.opt_bool("set_other_flow_ratios") && out.role != erBrim && out.role != erSkirt)
            out.first_layer_flow_ratio = config.opt_float("first_layer_flow_ratio");
        for (double factor : {out.process.flow_ratio, out.process.print_flow_ratio, out.role_flow_ratio, out.first_layer_flow_ratio})
            if (!std::isfinite(factor) || factor < 0.)
                throw std::invalid_argument("Hybrid core preview requires nonnegative finite native flow factors");
        out.nominal_supplied_mm3_per_mm = out.mm3_per_mm * out.process.flow_ratio * out.process.print_flow_ratio *
                                          out.role_flow_ratio * out.first_layer_flow_ratio;
        if (!std::isfinite(out.nominal_supplied_mm3_per_mm) || out.nominal_supplied_mm3_per_mm < 0.)
            throw std::invalid_argument("Invalid nominal conventional material supply");
        out.flow_geometry_unverified = out.process.flow_ratio * out.process.print_flow_ratio * out.role_flow_ratio *
                                           out.first_layer_flow_ratio != 1. || config.opt_bool("small_area_infill_flow_compensation");
        out.process.line_width   = out.width;
        out.process.layer_height = out.height;
        out.process.speed = nominal_speed(config, out.role, source.process.speed);
        if (out.nominal_supplied_mm3_per_mm > 0.)
            out.process.speed = std::min(out.process.speed, source.process.volumetric_limit / out.nominal_supplied_mm3_per_mm);
        for (const auto& p : path->polyline.points)
            out.points.emplace_back(unscale<double>(p.x() + shift.x()) + origin.x(), unscale<double>(p.y() + shift.y()) + origin.y(),
                                    z + origin.z());
        result.paths.push_back(std::move(out));
    } else
        throw std::invalid_argument("Unknown Orca extrusion entity");
    if (result.paths.size() > 1000000)
        throw std::invalid_argument("Hybrid core preview path limit exceeded");
}
void run_pass(Result& result,
              const indexed_triangle_set& mesh,
              const Source& source,
              int head,
              bool support_only,
              bool collect_support,
              const std::function<bool()>& canceled)
{
    if (canceled && canceled())
        throw CanceledException();
    auto config = configuration(source);
    Model model;
    auto* object = model.add_object();
    object->name = "Formorix hybrid core";
    object->add_volume(TriangleMesh(mesh));
    object->add_instance();
    Print print(Print::Purpose::HybridPreview);
    print.auto_assign_extruders(object);
    print.apply(model, config);
    const auto validation = print.validate();
    if (!validation.string.empty())
        throw std::invalid_argument(validation.string);
    print.set_status_callback([&](const PrintBase::SlicingStatus&) {
        if (canceled && canceled())
            print.cancel();
    });
    print.process();
    if (canceled && canceled())
        throw CanceledException();
    if (config.opt_bool("small_area_infill_flow_compensation"))
        result.warnings.push_back("Nominal conventional supply excludes Orca's small-area G-code flow compensation.");
    for (const auto* printed : print.objects()) {
        for (const auto& instance : printed->instances()) {
            if (!support_only)
                for (const auto* layer : printed->layers())
                    for (const auto* region : layer->regions()) {
                        append_entity(result, region->perimeters, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                      false, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON);
                        append_entity(result, region->fills, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                      false, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON);
                    }
            if (collect_support)
                for (const auto* layer : printed->support_layers())
                    append_entity(result, layer->support_fills, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                  true, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON);
        }
    }
}
} // namespace
Result slice(const FormorixGeometry::Mesh& core,
             const std::array<Source, 3>& sources,
             int core_head,
             int support_head,
             const std::function<bool()>& canceled)
{
    Result result;
    try {
        if (core_head < 0 || core_head > 2 || support_head < -1 || support_head > 2 || !FormorixGeometry::valid_solid(core))
            throw std::invalid_argument("Invalid conventional core or head assignment");
        const auto core_config = configuration(sources[core_head]);
        const bool supports    = core_config.opt_bool("enable_support") || core_config.opt_int("enforce_support_layers") > 0 ||
                                 core_config.opt_int("raft_layers") > 0;
        if (supports && support_head < 0)
            throw std::invalid_argument("Assign a support head before calculating enabled supports or a raft.");
        Vec3d minimum = core.vertices.front(), maximum = minimum;
        for (const auto& p : core.vertices) {
            minimum = minimum.cwiseMin(p);
            maximum = maximum.cwiseMax(p);
        }
        result.local_origin = Vec3d((minimum.x() + maximum.x()) / 2., (minimum.y() + maximum.y()) / 2., minimum.z());
        indexed_triangle_set mesh;
        FormorixGeometry::Mesh converted;
        for (const auto& p : core.vertices) {
            const Vec3f local             = (p - result.local_origin).cast<float>();
            const Vec3d restored          = local.cast<double>() + result.local_origin;
            result.float_conversion_bound = std::max(result.float_conversion_bound, (restored - p).norm());
            mesh.vertices.push_back(local);
            converted.vertices.push_back(restored);
        }
        mesh.indices    = core.faces;
        converted.faces = core.faces;
        if (!FormorixGeometry::valid_solid(converted))
            throw std::invalid_argument("Float conversion invalidates the conventional core");
        result.converted_volume_residual = FormorixGeometry::volume(converted) - FormorixGeometry::volume(core);
        // Conservative accounting for input/centering/output grid conversions.
        // This does not bound Orca's perimeter simplification or material deposition.
        result.coordinate_quantization_bound = 4. * std::sqrt(3.) * SCALING_FACTOR;
        run_pass(result, mesh, sources[core_head], core_head, false, supports && core_head == support_head, canceled);
        if (supports && support_head != core_head) {
            run_pass(result, mesh, sources[support_head], support_head, true, true, canceled);
            result.warnings.push_back(
                "Support paths use a separate Orca pass with the support head profile; no shared time schedule is implied.");
        }
        if (result.paths.empty())
            throw std::invalid_argument("Orca produced no conventional core or support extrusions for this volume.");
        result.warnings.push_back(
            "Conventional speeds are nominal role values; acceleration, cooling slowdown and common scheduling are not simulated.");
    } catch (const std::exception& error) {
        result.paths.clear();
        result.error = error.what();
    }
    return result;
}
} // namespace Slic3r::FormorixCorePreview
