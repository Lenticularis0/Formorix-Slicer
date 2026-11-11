#include "FormorixSurfaces.hpp"
#include "FormorixProcess.hpp"
#include "I18N.hpp"
#include <string>
#include <cstdint>
#include <exception>
#include <optional>
#include <vector>

#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleSelector.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r::FormorixSurfaces {
using nlohmann::json;
void Head::assign_material(const std::string& preset)
{
    if (preset != material_preset) process.reset();
    material_preset = preset;
}
void Head::assign_nozzle(const std::string& printer, unsigned index)
{
    if (printer != printer_preset || index != nozzle_index) process.reset();
    printer_preset = printer;
    nozzle_index = index;
}
void Head::assign_process(const std::string& preset)
{
    if (preset != process_preset) process.reset();
    process_preset = preset;
}
void Head::assign_width_limits(std::optional<FormorixProcess::WidthLimits> limits)
{
    if (limits && !FormorixProcess::valid(*limits))
        throw std::invalid_argument(L("Invalid declared local line width limits"));
    if (bool(limits) != bool(local_width_limits) ||
        (limits && (limits->minimum_mm != local_width_limits->minimum_mm || limits->maximum_mm != local_width_limits->maximum_mm)))
        process.reset();
    local_width_limits = limits;
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings, shell_mode, layers, shell_thickness, layer_height,
    preserve_dimensions, use_infill, inherit_infill_first, dense_underlay, underlay_density,
    additional_support, support_material, allow_deviation, max_deviation, allow_vertical,
    direction, custom_direction, edge, edge_selected, alternate_directions, fine_final_layers,
    fine_layer_height, boundary_margin, quality, ironing, head_mask, single_head)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Region, id, name, visible, continuous, settings)
void to_json(json& j, const Head& h)
{
    j = json{{"enabled", h.enabled}, {"material_preset", h.material_preset}, {"tasks", h.tasks},
        {"printer_preset", h.printer_preset}, {"nozzle_index", h.nozzle_index}, {"process_preset", h.process_preset}};
    if (h.local_width_limits) j["local_width_limits"] = *h.local_width_limits;
    if (h.process) j["process"] = *h.process;
}
void from_json(const json& j, Head& h)
{
    j.at("enabled").get_to(h.enabled); j.at("material_preset").get_to(h.material_preset); j.at("tasks").get_to(h.tasks);
    h.printer_preset = j.value("printer_preset", std::string{});
    h.process_preset = j.value("process_preset", std::string{});
    h.local_width_limits.reset();
    if (j.contains("local_width_limits") && !j.at("local_width_limits").is_null())
        h.local_width_limits = j.at("local_width_limits").get<FormorixProcess::WidthLimits>();
    if (j.contains("nozzle_index") && (!j.at("nozzle_index").is_number_integer() || j.at("nozzle_index") < 0 || j.at("nozzle_index") > 255))
        throw std::invalid_argument(_u8L("Invalid Formorix nozzle slot"));
    h.nozzle_index = j.value("nozzle_index", 0u);
    h.process.reset();
    if (j.contains("process")) h.process = j.at("process").get<FormorixProcess::Profile>();
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Transition, first, second, kind, width, overlap, pattern)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SelectionSettings, tool, brush_radius, limit_edges, edge_angle, slope_min, slope_max, section_position)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Project, version, regions, heads, strategy, transitions, selection)

Region *Project::find(int id)
{
    auto it = std::find_if(regions.begin(), regions.end(), [id](const Region &r) { return r.id == id; });
    return it == regions.end() ? nullptr : &*it;
}
const Region *Project::find(int id) const
{
    auto it = std::find_if(regions.begin(), regions.end(), [id](const Region &r) { return r.id == id; });
    return it == regions.end() ? nullptr : &*it;
}
int Project::next_id() const
{
    for (int id = 1; id <= max_regions; ++id)
        if (!find(id)) return id;
    return 0;
}

static bool in_range(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
template<class E> static bool enum_valid(E value, int last) { return int(value) >= 0 && int(value) <= last; }

static void validate(const Project &p)
{
    if (p.version != schema_version)
        throw std::invalid_argument(_u8L("Unsupported Formorix surface schema"));
    if (p.regions.size() > max_regions || !enum_valid(p.strategy, 2))
        throw std::invalid_argument(_u8L("Invalid Formorix project settings"));
    const auto &view = p.selection;
    if (view.tool < 0 || view.tool > 3 || !in_range(view.brush_radius, .4, 8.) ||
        !in_range(view.edge_angle, 0., 90.) || !in_range(view.slope_min, 0., 180.) ||
        !in_range(view.slope_max, 0., 180.) || !in_range(view.section_position, 0., 1.))
        throw std::invalid_argument(_u8L("Invalid Formorix selection settings"));
    std::set<int> ids;
    for (const Region &r : p.regions) {
        if (r.id < 1 || r.id > max_regions || !ids.insert(r.id).second || r.name.empty() || r.name.size() > 1024)
            throw std::invalid_argument(_u8L("Invalid or duplicate Formorix region"));
        const Settings &s = r.settings;
        if (!enum_valid(s.shell_mode, 2) || !enum_valid(s.support_material, 2) || !enum_valid(s.direction, 2) ||
            s.layers < 1 || s.layers > 10000 || !in_range(s.shell_thickness, 0.001, 10000.) ||
            !in_range(s.layer_height, 0.001, 100.) || !in_range(s.max_deviation, 0., 15.) ||
            !in_range(s.underlay_density, 0., 100.) || s.fine_final_layers < 0 || s.fine_final_layers > 10000 ||
            !in_range(s.fine_layer_height, 0.001, 100.) || !in_range(s.boundary_margin, 0., 10000.) ||
            s.quality < 0 || s.quality > 2 || s.head_mask == 0 || s.head_mask > 7 ||
            (s.single_head && (s.head_mask & (s.head_mask - 1)) != 0))
            throw std::invalid_argument(_u8L("Invalid Formorix region settings"));
        double norm = 0.;
        for (double v : s.custom_direction) {
            if (!in_range(v, -1e6, 1e6))
                throw std::invalid_argument(_u8L("Invalid surface direction"));
            norm += v * v;
        }
        if (norm < 1e-12)
            throw std::invalid_argument(_u8L("Surface direction must be nonzero"));
        for (double v : s.edge)
            if (!in_range(v, -1e9, 1e9))
                throw std::invalid_argument(_u8L("Invalid reference edge"));
    }
    for (const Head &h : p.heads)
        if (h.tasks == 0 || h.tasks > 31 || h.material_preset.size() > 4096 || h.printer_preset.size() > 4096 || h.nozzle_index > 255 ||
            h.process_preset.size() > 4096 || (h.local_width_limits && !FormorixProcess::valid(*h.local_width_limits)) ||
            (h.process && !FormorixProcess::valid(*h.process)))
            throw std::invalid_argument(_u8L("Invalid Formorix head assignment"));
    std::set<std::pair<int, int>> pairs;
    for (const Transition &t : p.transitions)
        if (!ids.count(t.first) || !ids.count(t.second) || t.first >= t.second ||
            !pairs.emplace(t.first, t.second).second || !enum_valid(t.kind, 2) || !enum_valid(t.pattern, 2) ||
            !in_range(t.width, 0.001, 10000.) || !in_range(t.overlap, 0., 100.))
            throw std::invalid_argument(_u8L("Invalid Formorix transition"));
}

std::string Project::encode() const { validate(*this); return json(*this).dump(); }
std::optional<Project> Project::decode(const std::string &text, std::string &error)
{
    error.clear();
    if (text.empty()) return Project{};
    try {
        if (text.size() > 4 * 1024 * 1024)
            throw std::invalid_argument(_u8L("Formorix settings exceed size limit"));
        auto data = json::parse(text);
        if (!data.is_object())
            throw std::invalid_argument(_u8L("Invalid Formorix surface object"));
        const int version = data.at("version").get<int>();
        if (version < 1 || version > schema_version)
            throw std::invalid_argument(_u8L("Unsupported Formorix surface schema"));
        if (data.contains("heads") && (!data["heads"].is_array() || data["heads"].size() != 3))
            throw std::invalid_argument(_u8L("Formorix requires three head assignments"));
        if (version < schema_version) {
            data["version"] = schema_version;
            for (auto& head : data.at("heads")) {
                head.erase("local_width_limits");
                head.erase("process_preset");
                if (head.contains("process") && head["process"].is_object())
                    head["process"].erase("local_width_limits");
            }
        }
        Project p = data.get<Project>();
        validate(p);
        return p;
    } catch (const std::exception &e) { error = e.what(); return std::nullopt; }
}

std::uint64_t topology_id(const indexed_triangle_set &mesh)
{
    std::uint64_t hash = 14695981039346656037ULL;
    auto add = [&hash](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) { hash ^= value & 255; hash *= 1099511628211ULL; value >>= 8; }
    };
    add(mesh.vertices.size());
    add(mesh.indices.size());
    for (const auto &triangle : mesh.indices)
        for (int i = 0; i < 3; ++i) add(triangle[i]);
    return hash;
}
bool painting_matches(const ModelVolume &v)
{
    return v.formorix_facets.empty() || v.formorix_topology == topology_id(v.mesh().its);
}

void remap_regions(Model &model, const std::vector<int> &sources, int destination)
{
    if (destination < 0 || destination > max_regions)
        throw std::invalid_argument(_u8L("Invalid region destination"));
    EnforcerBlockerStateMap map;
    for (int i = 0; i <= max_regions; ++i) map[i] = EnforcerBlockerType(i);
    for (int id : sources) {
        if (id < 1 || id > max_regions)
            throw std::invalid_argument(_u8L("Invalid region source"));
        map[id] = EnforcerBlockerType(destination);
    }
    for (ModelObject *object : model.objects)
        for (ModelVolume *volume : object->volumes) {
            if (volume->formorix_facets.empty() || !painting_matches(*volume)) continue;
            TriangleSelector selector(volume->mesh());
            selector.deserialize(volume->formorix_facets.get_data());
            selector.remap_triangle_state(map);
            volume->formorix_facets.set(selector);
        }
}

void merge_regions(Project &p, Model &model, const std::vector<int> &ids, int destination)
{
    Region *target = p.find(destination);
    if (!target || ids.size() < 2 || std::find(ids.begin(), ids.end(), destination) == ids.end())
        throw std::invalid_argument(_u8L("Select at least two regions including the destination"));
    for (int id : ids)
        if (!p.find(id))
            throw std::invalid_argument(_u8L("Unknown region"));
    remap_regions(model, ids, destination);
    target->continuous = true;
    auto selected = [&ids](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    p.regions.erase(std::remove_if(p.regions.begin(), p.regions.end(), [&](const Region &r) {
        return r.id != destination && selected(r.id);
    }), p.regions.end());
    std::set<std::pair<int, int>> pairs;
    p.transitions.erase(std::remove_if(p.transitions.begin(), p.transitions.end(), [&](Transition &t) {
        if (selected(t.first)) t.first = destination;
        if (selected(t.second)) t.second = destination;
        if (t.first > t.second) std::swap(t.first, t.second);
        return t.first == t.second || !pairs.emplace(t.first, t.second).second;
    }), p.transitions.end());
}
void remove_regions(Project &p, Model &model, const std::vector<int> &ids)
{
    remap_regions(model, ids, 0);
    auto selected = [&ids](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    p.regions.erase(std::remove_if(p.regions.begin(), p.regions.end(), [&](const Region &r) { return selected(r.id); }), p.regions.end());
    p.transitions.erase(std::remove_if(p.transitions.begin(), p.transitions.end(), [&](const Transition &t) {
        return selected(t.first) || selected(t.second);
    }), p.transitions.end());
}
}
