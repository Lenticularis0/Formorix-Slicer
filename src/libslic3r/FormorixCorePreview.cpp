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
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Geometry.hpp"
#include "Polygon.hpp"
#include "Triangulation.hpp"
#include "Slicing.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "libslic3r.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <string>
#include <vector>

namespace Slic3r::FormorixCorePreview {
namespace {
void check_cancel(const std::function<bool()>& canceled)
{
    if (canceled && canceled())
        throw CanceledException();
}

boost::multiprecision::int128_t twice_area(const ExPolygons& polygons)
{
    boost::multiprecision::int128_t total = 0;
    for (const auto& polygon : polygons)
        for (std::size_t ring = 0; ring < polygon.num_contours(); ++ring) {
            const auto& points = polygon.contour_or_hole(ring).points;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto& a = points[i];
                const auto& b = points[(i + 1) % points.size()];
                total += boost::multiprecision::int128_t(a.x()) * b.y() - boost::multiprecision::int128_t(a.y()) * b.x();
            }
        }
    return total;
}

void validate_requests(const std::vector<DenseRequest>& requests, const std::function<bool()>& canceled)
{
    if (requests.size() > 256)
        throw std::invalid_argument(_u8L("Too many local dense-underlay requests"));
    std::size_t points = 0, comparisons = 0;
    for (const auto& request : requests) {
        check_cancel(canceled);
        if (request.region <= 0 || !std::isfinite(request.density) || request.density < 0. || request.density > 100. ||
            !std::isfinite(request.bottom_z) || !std::isfinite(request.top_z) || !std::isfinite(request.slice_z) ||
            request.top_z <= request.bottom_z || request.slice_z <= request.bottom_z || request.slice_z >= request.top_z ||
            std::abs(request.bottom_z) > 1e7 || std::abs(request.top_z) > 1e7 || request.area.empty())
            throw std::invalid_argument(_u8L("Invalid local dense-underlay density, slice interval or area"));
        const auto count = count_points(request.area);
        if (count > 2048 || count > 65536 - points)
            throw std::invalid_argument(_u8L("Local dense-underlay polygon budget exceeded"));
        points += count;
        std::vector<const Polygon*> rings;
        Point low = request.area.front().contour.points.empty() ? Point(0, 0) : request.area.front().contour.points.front(), high = low;
        for (const auto& area : request.area) {
            if (!area.is_valid() || area.area() <= 0.)
                throw std::invalid_argument(_u8L("Local dense-underlay contours must have valid outer and hole orientations"));
            rings.push_back(&area.contour);
            for (const auto& hole : area.holes) {
                if (!area.contour.contains(hole.points.front()))
                    throw std::invalid_argument(_u8L("A local dense-underlay hole lies outside its contour"));
                rings.push_back(&hole);
            }
        }
        for (const auto* ring : rings) {
            std::set<std::pair<coord_t, coord_t>> unique;
            for (const auto& p : ring->points) {
                if (std::abs(unscale<double>(p.x())) > 1e7 || std::abs(unscale<double>(p.y())) > 1e7 ||
                    !unique.emplace(p.x(), p.y()).second)
                    throw std::invalid_argument(_u8L("Invalid or repeated local dense-underlay polygon vertex"));
                low = low.cwiseMin(p);
                high = high.cwiseMax(p);
            }
        }
        // Existing integer segment predicates use 64-bit cross products. Keep
        // both differences <=2e9 scaled units, including the 1600-mm machine.
        if ((high - low).maxCoeff() > scale_(2000.))
            throw std::invalid_argument(_u8L("Local dense-underlay area exceeds the supported geometric span"));
        for (std::size_t r = 0; r < rings.size(); ++r)
            for (std::size_t s = r; s < rings.size(); ++s)
                for (std::size_t i = 0; i < rings[r]->points.size(); ++i)
                    for (std::size_t j = r == s ? i + 1 : 0; j < rings[s]->points.size(); ++j) {
                        if (++comparisons > 2000000)
                            throw std::invalid_argument(_u8L("Local dense-underlay intersection budget exceeded"));
                        if ((comparisons & 255) == 0)
                            check_cancel(canceled);
                        const auto ni = (i + 1) % rings[r]->points.size(), nj = (j + 1) % rings[s]->points.size();
                        if (r == s && (ni == j || nj == i))
                            continue;
                        if (Geometry::segments_intersect(rings[r]->points[i], rings[r]->points[ni], rings[s]->points[j], rings[s]->points[nj]))
                            throw std::invalid_argument(_u8L("Local dense-underlay boundaries intersect or touch themselves"));
                    }
        if (twice_area(request.area) != twice_area(union_ex(request.area)))
            throw std::invalid_argument(_u8L("Local dense-underlay components or holes overlap"));
    }
}

std::vector<LayerSlice> layer_slices(const Print& print, const Result& result, int head, const std::function<bool()>& canceled)
{
    std::vector<LayerSlice> out;
    const Point origin(scale_(result.local_origin.x()), scale_(result.local_origin.y()));
    for (const auto* object : print.objects())
        for (const auto& instance : object->instances())
            for (const auto* layer : object->layers()) {
                check_cancel(canceled);
                LayerSlice slice;
                slice.layer_id = int(layer->id());
                slice.head = head;
                slice.area = layer->lslices;
                translate(slice.area, instance.shift + origin);
                for (const auto* region : layer->regions())
                    slice.region_area.insert(slice.region_area.end(), region->raw_slices.begin(), region->raw_slices.end());
                slice.region_area = union_ex(slice.region_area);
                translate(slice.region_area, instance.shift + origin);
                slice.slice_z = layer->slice_z + result.local_origin.z();
                slice.print_bottom_z = layer->bottom_z() + result.local_origin.z();
                slice.print_top_z = layer->print_z + result.local_origin.z();
                slice.bottom_z = slice.print_bottom_z - object->slicing_parameters().object_print_z_min;
                slice.top_z = slice.print_top_z - object->slicing_parameters().object_print_z_min;
                out.push_back(std::move(slice));
            }
    return out;
}

double modifier_conversion_bound(const Result& result, const ExPolygons& area, double bottom, double top)
{
    const Point shift(scale_(result.local_origin.x()), scale_(result.local_origin.y()));
    Vec3d maximum = Vec3d::Zero();
    for (const auto& p : to_points(area)) {
        const auto q = p - shift;
        maximum = maximum.cwiseMax(Vec3d(std::abs(unscale<double>(q.x())), std::abs(unscale<double>(q.y())), 0.));
    }
    maximum.z() = std::max(std::abs(bottom - result.local_origin.z()), std::abs(top - result.local_origin.z()));
    const double xy_rounding = (Vec2d(unscale<double>(shift.x()), unscale<double>(shift.y())) - result.local_origin.head<2>()).norm();
    return std::nextafter(xy_rounding + maximum.norm() * std::numeric_limits<float>::epsilon(),
                          std::numeric_limits<double>::infinity());
}

indexed_triangle_set modifier_mesh(Result& result, const ExPolygons& area, double bottom, double top)
{
    ExPolygons local = area;
    const Point shift(scale_(result.local_origin.x()), scale_(result.local_origin.y()));
    translate(local, -shift);
    double planar_area = 0., perimeter = 0.;
    std::size_t holes = 0;
    for (const auto& polygon : local) {
        planar_area += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        holes += polygon.holes.size();
        for (std::size_t ring = 0; ring < polygon.num_contours(); ++ring) {
            const auto& contour = polygon.contour_or_hole(ring);
            perimeter += contour.length() * SCALING_FACTOR;
        }
    }
    const double lo = bottom - result.local_origin.z(), hi = top - result.local_origin.z();
    const double displacement = modifier_conversion_bound(result, area, bottom, top);
    // One indexed boundary shared by caps and walls. Independent GLU cap
    // tessellation may discard a collinear boundary vertex and leave a T-junction.
    indexed_triangle_set mesh;
    const Points points = to_points(local);
    std::set<std::pair<coord_t, coord_t>> unique;
    for (const auto& point : points)
        if (!unique.emplace(point.x(), point.y()).second)
            throw std::invalid_argument(_u8L("Local dense-underlay modifier boundaries touch after clipping"));
    const auto triangles = Triangulation::triangulate(local, points);
    const int n = int(points.size());
    for (double z : {lo, hi})
        for (const auto& p : points)
            mesh.vertices.emplace_back(float(unscale<double>(p.x())), float(unscale<double>(p.y())), float(z));
    for (const auto& t : triangles) {
        mesh.indices.emplace_back(t[2], t[1], t[0]);
        mesh.indices.emplace_back(t[0] + n, t[1] + n, t[2] + n);
    }
    int first = 0;
    for (const auto& polygon : local)
        for (std::size_t ring = 0; ring < polygon.num_contours(); ++ring) {
            const int count = int(polygon.contour_or_hole(ring).points.size());
            for (int i = 0; i < count; ++i) {
                const int a = first + i, b = first + (i + 1) % count;
                mesh.indices.emplace_back(a, b, a + n);
                mesh.indices.emplace_back(b, b + n, a + n);
            }
            first += count;
        }
    if (mesh.indices.size() > 100000 || mesh.vertices.size() > 100000)
        throw std::invalid_argument(_u8L("Local dense-underlay modifier mesh budget exceeded"));
    FormorixGeometry::Mesh geometry;
    geometry.faces = mesh.indices;
    for (const auto& p : mesh.vertices)
        geometry.vertices.push_back(p.cast<double>());
    if (!FormorixGeometry::valid_solid(geometry))
        throw std::invalid_argument(_u8L("Local dense-underlay modifier cannot be represented as a valid native solid"));
    std::set<std::pair<int, int>> edges;
    for (const auto& face : mesh.indices)
        for (int i = 0; i < 3; ++i)
            edges.insert(std::minmax(face[i], face[(i + 1) % 3]));
    const auto euler = std::ptrdiff_t(mesh.vertices.size()) - std::ptrdiff_t(edges.size()) + std::ptrdiff_t(mesh.indices.size());
    if (euler != 2 * (std::ptrdiff_t(local.size()) - std::ptrdiff_t(holes)))
        throw std::invalid_argument(_u8L("Native modifier conversion changes a local dense-underlay component or hole"));
    const double expected = planar_area * (top - bottom);
    const double error = std::abs(FormorixGeometry::volume(geometry) - expected);
    const double allowance = 8. * displacement * (2. * planar_area + perimeter * (top - bottom)) +
                             128. * std::numeric_limits<double>::epsilon() * expected;
    if (!(expected > 0.) || !std::isfinite(error) || error > allowance)
        throw std::invalid_argument(_u8L("Native modifier conversion does not preserve the requested dense-underlay volume"));
    result.modifier_float_conversion_bound = std::max(result.modifier_float_conversion_bound, displacement);
    result.modifier_volume_error = std::max(result.modifier_volume_error, error);
    return mesh;
}

void add_dense_modifiers(Result& result, ModelObject& object, const std::vector<LayerSlice>& layers,
                         const std::vector<DenseRequest>& requests, double baseline, const std::function<bool()>& canceled)
{
    using Interval = std::tuple<double, double, double>;
    std::map<Interval, std::map<double, ExPolygons, std::greater<double>>> grouped;
    std::set<int> regions;
    for (const auto& request : requests) {
        check_cancel(canceled);
        const auto layer = std::find_if(layers.begin(), layers.end(), [&](const LayerSlice& candidate) {
            return candidate.bottom_z == request.bottom_z && candidate.top_z == request.top_z && candidate.slice_z == request.slice_z;
        });
        if (layer == layers.end())
            throw std::invalid_argument(_u8L("Local dense-underlay request no longer matches the current native slice"));
        if (!diff_ex(request.area, layer->area).empty())
            throw std::invalid_argument(_u8L("Local dense-underlay request extends outside the native slice"));
        if (request.density <= baseline)
            continue;
        auto& area = grouped[{request.bottom_z, request.top_z, request.slice_z}][request.density];
        area.insert(area.end(), request.area.begin(), request.area.end());
        ++result.dense_requests_applied;
        regions.insert(request.region);
    }
    for (const auto& layer : grouped) {
        const auto native = std::find_if(layers.begin(), layers.end(), [&](const LayerSlice& candidate) {
            return candidate.bottom_z == std::get<0>(layer.first) && candidate.top_z == std::get<1>(layer.first) &&
                   candidate.slice_z == std::get<2>(layer.first);
        });
        ExPolygons occupied;
        for (const auto& requested : layer.second) {
            check_cancel(canceled);
            const auto total = union_ex(requested.second);
            const auto requested_area = diff_ex(total, occupied);
            occupied.insert(occupied.end(), total.begin(), total.end());
            occupied = union_ex(occupied);
            if (requested_area.empty())
                continue;
            // A Float prism coincident with a sliced mesh edge can straddle it
            // by a few grid units. Native intersection/difference then round
            // different intersections and may remove a real sliver of the core.
            // Restrict only the density mask, by its measured conversion/grid
            // uncertainty, and still require an exactly unchanged raw core below.
            const double conversion = modifier_conversion_bound(result, requested_area, std::get<0>(layer.first), std::get<1>(layer.first));
            const double retreat = std::ceil((conversion + result.coordinate_quantization_bound) / SCALING_FACTOR);
            const auto disjoint = intersection_ex(requested_area, offset_ex(native->region_area, -float(retreat)));
            if (disjoint.empty())
                throw std::invalid_argument(_u8L("Local dense-underlay area disappears within its native conversion uncertainty"));
            result.dense_requested_area += twice_area(requested_area).convert_to<double>() * .5 * SCALING_FACTOR * SCALING_FACTOR;
            result.dense_applied_area += twice_area(disjoint).convert_to<double>() * .5 * SCALING_FACTOR * SCALING_FACTOR;
            // This records construction offset extent only. Thin components may
            // disappear, so it is not a Hausdorff certificate for the whole mask.
            if (!diff_ex(requested_area, disjoint).empty())
                result.modifier_boundary_retreat_bound = std::max(result.modifier_boundary_retreat_bound, std::nextafter(
                    (DefaultMiterLimit * retreat + std::sqrt(2.)) * SCALING_FACTOR, std::numeric_limits<double>::infinity()));
            auto mesh = modifier_mesh(result, disjoint, std::get<0>(layer.first), std::get<1>(layer.first));
            if (mesh.indices.size() > 200000 - result.dense_modifier_triangles)
                throw std::invalid_argument(_u8L("Combined local dense-underlay modifier mesh budget exceeded"));
            result.dense_modifier_triangles += mesh.indices.size();
            // Keep the generated shared Float coordinates. Re-centering each
            // modifier would add another independent rounding at its boundary.
            auto* modifier = object.add_volume(TriangleMesh(std::move(mesh)), ModelVolumeType::PARAMETER_MODIFIER, false);
            modifier->name = _u8L("Formorix dense underlay");
            modifier->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(requested.first));
            ++result.dense_modifier_volumes;
        }
    }
    result.applied_dense_regions.assign(regions.begin(), regions.end());
}

void project_options(DynamicPrintConfig& config, const std::vector<std::string>& keys, std::size_t index)
{
    for (const auto& key : keys) {
        auto* value = dynamic_cast<ConfigOptionVectorBase*>(config.option(key));
        if (!value || value->size() == 0)
            continue;
        // A single slot is an inherited per-printer default, not another nozzle's data.
        if (index >= value->size() && value->size() != 1)
            throw std::invalid_argument(format(_u8L("Missing explicit per-head Orca option: %1%"), key));
        std::unique_ptr<ConfigOption> original(value->clone());
        value->resize(1);
        value->set_at(original.get(), 0,
                      original->is_vector() && index < static_cast<ConfigOptionVectorBase*>(original.get())->size() ? index : 0);
    }
}
DynamicPrintConfig configuration(const Source& source)
{
    if (!FormorixProcess::valid(source.process))
        throw std::invalid_argument(_u8L("Invalid resolved head profile"));
    auto config = DynamicPrintConfig::full_print_config();
    config.apply(source.config);
    if (config.opt_bool("belt_printer") || config.opt_bool("spiral_mode"))
        throw std::invalid_argument(_u8L("Hybrid core preview does not support belt or spiral mode"));
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
            throw std::invalid_argument(format(_u8L("Missing paired machine limit: %1%"), key));
        std::unique_ptr<ConfigOption> original(value->clone());
        const std::size_t second = value->size() == 1 ? 0 : first + 1;
        value->resize(2);
        value->set_at(original.get(), 0, first);
        value->set_at(original.get(), 1, second);
    }
    const auto type   = ExtruderType(config.option<ConfigOptionEnumsGeneric>("extruder_type")->get_at(0));
    const auto volume = NozzleVolumeType(config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->get_at(0));
    if (volume == nvtHybrid)
        throw std::invalid_argument(_u8L("Hybrid core preview requires one concrete nozzle volume type per head"));
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
                _u8L("Hybrid core preview requires neutral XY/Z shrink compensation until core and shell share a compensation transform."));
    for (const char* key : {"filament_shrink", "filament_shrinkage_compensation_z"}) {
        const auto* values = config.option<ConfigOptionPercents>(key);
        if (!values || values->values.empty() || !std::isfinite(values->get_at(0)) || std::abs(values->get_at(0) - 100.) > 1e-10)
            throw std::invalid_argument(
                format(_u8L("Hybrid core preview requires 100%% %1%; automatic shrink neutralization is not applied."), key));
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
                   bool first_layer,
                   bool dense_transition = false,
                   double native_density = 0.,
                   const std::function<bool()>& canceled = {})
{
    check_cancel(canceled);
    if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
        for (const auto* child : collection->entities)
            append_entity(result, *child, z, shift, origin, head, source, config, support, first_layer, dense_transition, native_density, canceled);
    } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        for (const auto& path : loop->paths)
            append_entity(result, path, z, shift, origin, head, source, config, support, first_layer, dense_transition, native_density, canceled);
    } else if (const auto* multi = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
        for (const auto& path : multi->paths)
            append_entity(result, path, z, shift, origin, head, source, config, support, first_layer, dense_transition, native_density, canceled);
    } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        if (path->is_force_no_extrusion() || path->polyline.points.size() < 2)
            return;
        if (dynamic_cast<const ExtrusionPathSloped*>(path) || path->z_contoured)
            throw std::invalid_argument(_u8L("Sloped or contoured conventional extrusions are not supported by hybrid preview"));
        Path out;
        out.head                 = head;
        out.role                 = path->role();
        out.support              = support;
        out.dense_transition     = dense_transition && !support;
        out.native_infill_density = support ? 0. : native_density;
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
                throw std::invalid_argument(_u8L("Hybrid core preview requires nonnegative finite native flow factors"));
        out.nominal_supplied_mm3_per_mm = out.mm3_per_mm * out.process.flow_ratio * out.process.print_flow_ratio *
                                          out.role_flow_ratio * out.first_layer_flow_ratio;
        if (!std::isfinite(out.nominal_supplied_mm3_per_mm) || out.nominal_supplied_mm3_per_mm < 0.)
            throw std::invalid_argument(_u8L("Invalid nominal conventional material supply"));
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
        throw std::invalid_argument(_u8L("Unknown Orca extrusion entity"));
    if (result.paths.size() > 1000000)
        throw std::invalid_argument(_u8L("Hybrid core preview path limit exceeded"));
}
void run_pass(Result& result,
              const indexed_triangle_set& mesh,
              const Source& source,
              int head,
              bool support_only,
              bool collect_support,
              const std::function<bool()>& canceled,
              const std::vector<DenseRequest>& requests = {})
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
    check_cancel(canceled);
    const auto* density_option = config.option<ConfigOptionPercent>("sparse_infill_density");
    if (!density_option || !std::isfinite(density_option->value) || density_option->value < 0. || density_option->value > 100.)
        throw std::invalid_argument(_u8L("Invalid native sparse-infill density for local underlay"));
    const double baseline_density = density_option->value;
    if (!support_only) {
        const auto baseline_layers = layer_slices(print, result, head, canceled);
        if (!requests.empty()) {
            add_dense_modifiers(result, *object, baseline_layers, requests, baseline_density, canceled);
            if (result.dense_modifier_volumes > 0) {
                print.apply(model, config);
                const auto modified_validation = print.validate();
                if (!modified_validation.string.empty())
                    throw std::invalid_argument(modified_validation.string);
                print.process();
                check_cancel(canceled);
            }
        }
        result.layers = layer_slices(print, result, head, canceled);
        if (result.layers.size() != baseline_layers.size())
            throw std::invalid_argument(_u8L("Local dense-underlay modifiers changed the native layer intervals"));
        for (std::size_t i = 0; i < result.layers.size(); ++i) {
            check_cancel(canceled);
            const auto& actual = result.layers[i];
            const auto& expected = baseline_layers[i];
            if (actual.slice_z != expected.slice_z || actual.bottom_z != expected.bottom_z || actual.top_z != expected.top_z ||
                actual.print_bottom_z != expected.print_bottom_z || actual.print_top_z != expected.print_top_z ||
                !diff_ex(actual.region_area, expected.region_area).empty() || !diff_ex(expected.region_area, actual.region_area).empty())
                throw std::invalid_argument(_u8L("Local dense-underlay modifiers changed the native slice geometry"));
            if (!diff_ex(actual.area, expected.area).empty() || !diff_ex(expected.area, actual.area).empty()) {
                // Layer::make_slices copies one region directly, but applies
                // this exact native safety offset as soon as two regions exist.
                // Keep the actual native lslices; never substitute the baseline
                // or accept an arbitrary near-equal shape.
                const auto inflated = union_safety_offset_ex(expected.area);
                if (!diff_ex(actual.area, inflated).empty() || !diff_ex(inflated, actual.area).empty())
                    throw std::invalid_argument(_u8L("Local dense-underlay modifiers changed the native slice beyond its safety offset"));
                // Clipper limits a miter to M*delta; square fallback is shorter.
                // Add a full grid diagonal for rounded offset coordinates.
                result.native_slice_expansion_bound = std::nextafter(
                    (DefaultMiterLimit * ClipperSafetyOffset + std::sqrt(2.)) * SCALING_FACTOR,
                    std::numeric_limits<double>::infinity());
            }
        }
        if (result.modifier_boundary_retreat_bound > 0.)
            result.warnings.push_back(_u8L("Local dense-underlay masks leave a boundary strip within native conversion uncertainty; the core is unchanged."));
        if (result.native_slice_expansion_bound > 0.)
            result.warnings.push_back(_u8L("Native merged slice outlines include Orca's safety expansion; their raw region union remains unchanged."));
    }
    if (config.opt_bool("small_area_infill_flow_compensation"))
        result.warnings.push_back(_u8L("Nominal conventional supply excludes Orca's small-area G-code flow compensation."));
    for (const auto* printed : print.objects()) {
        for (const auto& instance : printed->instances()) {
            if (!support_only)
                for (const auto* layer : printed->layers())
                    for (const auto* region : layer->regions()) {
                        const double density = region->region().config().sparse_infill_density.value;
                        const bool dense = density > baseline_density;
                        append_entity(result, region->perimeters, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                      false, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON, dense, density, canceled);
                        append_entity(result, region->fills, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                      false, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON, dense, density, canceled);
                    }
            if (collect_support)
                for (const auto* layer : printed->support_layers())
                    append_entity(result, layer->support_fills, layer->print_z, instance.shift, result.local_origin, head, source, config,
                                  true, layer->id() == 0 && std::abs(layer->bottom_z()) < EPSILON, false, 0., canceled);
        }
    }
}
} // namespace
Result slice(const FormorixGeometry::Mesh& core,
             const std::array<Source, 3>& sources,
             int core_head,
             int support_head,
             const std::function<bool()>& canceled,
             const std::vector<DenseRequest>& requests)
{
    Result result;
    try {
        check_cancel(canceled);
        validate_requests(requests, canceled);
        if (core_head < 0 || core_head > 2 || support_head < -1 || support_head > 2 || !FormorixGeometry::valid_solid(core))
            throw std::invalid_argument(_u8L("Invalid conventional core or head assignment"));
        const auto core_config = configuration(sources[core_head]);
        const bool supports    = core_config.opt_bool("enable_support") || core_config.opt_int("enforce_support_layers") > 0 ||
                                 core_config.opt_int("raft_layers") > 0;
        if (supports && support_head < 0)
            throw std::invalid_argument(_u8L("Assign a support head before calculating enabled supports or a raft."));
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
            throw std::invalid_argument(_u8L("Float conversion invalidates the conventional core"));
        result.converted_volume_residual = FormorixGeometry::volume(converted) - FormorixGeometry::volume(core);
        // Conservative accounting for input/centering/output grid conversions.
        // This does not bound Orca's perimeter simplification or material deposition.
        result.coordinate_quantization_bound = 4. * std::sqrt(3.) * SCALING_FACTOR;
        run_pass(result, mesh, sources[core_head], core_head, false, supports && core_head == support_head, canceled, requests);
        if (supports && support_head != core_head) {
            run_pass(result, mesh, sources[support_head], support_head, true, true, canceled);
            result.warnings.push_back(
                _u8L("Support paths use a separate Orca pass with the support head profile; no shared time schedule is implied."));
        }
        if (result.paths.empty())
            throw std::invalid_argument(_u8L("Orca produced no conventional core or support extrusions for this volume."));
        result.warnings.push_back(
            _u8L("Conventional speeds are nominal role values; acceleration, cooling slowdown and common scheduling are not simulated."));
        check_cancel(canceled);
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}
} // namespace Slic3r::FormorixCorePreview
