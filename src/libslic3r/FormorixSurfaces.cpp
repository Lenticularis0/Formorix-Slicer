#include "FormorixSurfaces.hpp"
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

namespace Slic3r::FormorixSurfaces {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings, shell_mode, layers, shell_thickness, layer_height,
    preserve_dimensions, use_infill, inherit_infill_first, dense_underlay, underlay_density,
    additional_support, support_material, allow_deviation, max_deviation, allow_vertical,
    direction, custom_direction, edge, edge_selected, alternate_directions, fine_final_layers,
    fine_layer_height, boundary_margin, quality, ironing, head_mask, single_head)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Region, id, name, visible, continuous, settings)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Head, enabled, material_preset, tasks)
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
    if (p.version != schema_version) throw std::invalid_argument("Unsupported Formorix surface schema");
    if (p.regions.size() > max_regions || !enum_valid(p.strategy, 2))
        throw std::invalid_argument("Invalid Formorix project settings");
    const auto &view = p.selection;
    if (view.tool < 0 || view.tool > 3 || !in_range(view.brush_radius, .4, 8.) ||
        !in_range(view.edge_angle, 0., 90.) || !in_range(view.slope_min, 0., 180.) ||
        !in_range(view.slope_max, 0., 180.) || !in_range(view.section_position, 0., 1.))
        throw std::invalid_argument("Invalid Formorix selection settings");
    std::set<int> ids;
    for (const Region &r : p.regions) {
        if (r.id < 1 || r.id > max_regions || !ids.insert(r.id).second || r.name.empty() || r.name.size() > 1024)
            throw std::invalid_argument("Invalid or duplicate Formorix region");
        const Settings &s = r.settings;
        if (!enum_valid(s.shell_mode, 2) || !enum_valid(s.support_material, 2) || !enum_valid(s.direction, 2) ||
            s.layers < 1 || s.layers > 10000 || !in_range(s.shell_thickness, 0.001, 10000.) ||
            !in_range(s.layer_height, 0.001, 100.) || !in_range(s.max_deviation, 0., 15.) ||
            !in_range(s.underlay_density, 0., 100.) || s.fine_final_layers < 0 || s.fine_final_layers > 10000 ||
            !in_range(s.fine_layer_height, 0.001, 100.) || !in_range(s.boundary_margin, 0., 10000.) ||
            s.quality < 0 || s.quality > 2 || s.head_mask == 0 || s.head_mask > 7 ||
            (s.single_head && (s.head_mask & (s.head_mask - 1)) != 0))
            throw std::invalid_argument("Invalid Formorix region settings");
        double norm = 0.;
        for (double v : s.custom_direction) {
            if (!in_range(v, -1e6, 1e6)) throw std::invalid_argument("Invalid surface direction");
            norm += v * v;
        }
        if (norm < 1e-12) throw std::invalid_argument("Surface direction must be nonzero");
        for (double v : s.edge)
            if (!in_range(v, -1e9, 1e9)) throw std::invalid_argument("Invalid reference edge");
    }
    for (const Head &h : p.heads)
        if (h.tasks == 0 || h.tasks > 31 || h.material_preset.size() > 4096)
            throw std::invalid_argument("Invalid Formorix head assignment");
    std::set<std::pair<int, int>> pairs;
    for (const Transition &t : p.transitions)
        if (!ids.count(t.first) || !ids.count(t.second) || t.first >= t.second ||
            !pairs.emplace(t.first, t.second).second || !enum_valid(t.kind, 2) || !enum_valid(t.pattern, 2) ||
            !in_range(t.width, 0.001, 10000.) || !in_range(t.overlap, 0., 100.))
            throw std::invalid_argument("Invalid Formorix transition");
}

std::string Project::encode() const { validate(*this); return json(*this).dump(); }
std::optional<Project> Project::decode(const std::string &text, std::string &error)
{
    error.clear();
    if (text.empty()) return Project{};
    try {
        if (text.size() > 4 * 1024 * 1024) throw std::invalid_argument("Formorix settings exceed size limit");
        const auto data = json::parse(text);
        if (!data.is_object() || data.at("version").get<int>() != schema_version)
            throw std::invalid_argument("Unsupported Formorix surface schema");
        if (data.contains("heads") && (!data["heads"].is_array() || data["heads"].size() != 3))
            throw std::invalid_argument("Formorix requires three head assignments");
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
    if (destination < 0 || destination > max_regions) throw std::invalid_argument("Invalid region destination");
    EnforcerBlockerStateMap map;
    for (int i = 0; i <= max_regions; ++i) map[i] = EnforcerBlockerType(i);
    for (int id : sources) {
        if (id < 1 || id > max_regions) throw std::invalid_argument("Invalid region source");
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
        throw std::invalid_argument("Select at least two regions including the destination");
    for (int id : ids) if (!p.find(id)) throw std::invalid_argument("Unknown region");
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
