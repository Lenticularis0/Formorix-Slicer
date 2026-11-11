#pragma once
#include "FormorixProcess.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct indexed_triangle_set;
namespace Slic3r {
class Model;
class ModelVolume;

// Project annotations for the experimental preview; the normal print pipeline
// never treats these manufacturing wishes as executable movements.
namespace FormorixSurfaces {
constexpr int schema_version = 3;
// Reuse Orca's subdivision-aware painting codec, independently of filament painting.
constexpr int max_regions = 32;
constexpr const char *archive_path = "Metadata/formorix_surfaces.json";

enum class ShellMode { Layers, Thickness, FullWall };
enum class SupportMaterial { Breakaway, Soluble, Automatic };
enum class Direction { Automatic, Edge, Custom };
enum class Strategy { AutomaticParallel, MaterialRoles, HighSpeed };
enum class TransitionKind { Fixed, Interwoven, Parallel };
enum class TransitionPattern { Automatic, Toothed, AlternatingLayers };
enum HeadTask { Build = 1, Surface = 2, Support = 4, Reinforcement = 8, Automatic = 16 };

struct Settings {
    ShellMode shell_mode = ShellMode::Layers;
    int layers = 5;
    double shell_thickness = 1.0;
    double layer_height = 0.2;
    bool preserve_dimensions = true;
    bool use_infill = true;
    bool inherit_infill_first = true;
    bool dense_underlay = false;
    double underlay_density = 60.;
    bool additional_support = false;
    SupportMaterial support_material = SupportMaterial::Automatic;
    bool allow_deviation = false;
    double max_deviation = 15.;
    bool allow_vertical = false;
    Direction direction = Direction::Automatic;
    std::array<double, 3> custom_direction{1., 0., 0.};
    // Edge endpoints in the painted volume's local coordinates; captured by a GUI selection.
    std::array<double, 6> edge{};
    bool edge_selected = false;
    bool alternate_directions = true;
    int fine_final_layers = 0;
    double fine_layer_height = 0.1;
    double boundary_margin = 0.;
    int quality = 1;
    bool ironing = false;
    unsigned head_mask = 1;
    bool single_head = true;
};

struct Region {
    int id = 0;
    std::string name;
    bool visible = true;
    bool continuous = false;
    Settings settings;
};

struct Head {
    bool enabled = true;
    std::string material_preset;
    unsigned tasks = HeadTask::Automatic;
    // Optional informational snapshot. Re-resolve current Orca bindings before
    // every calculation; a persisted snapshot is never an authoritative source.
    std::optional<FormorixProcess::Profile> process;
    void assign_material(const std::string& preset);
    // Empty printer name explicitly follows the currently edited Orca printer.
    // This is a nozzle-source binding, not a separate printer/process manager.
    std::string printer_preset;
    unsigned nozzle_index = 0;
    void assign_nozzle(const std::string& printer, unsigned index);
};

struct Transition {
    int first = 0;
    int second = 0;
    TransitionKind kind = TransitionKind::Fixed;
    double width = 2.;
    double overlap = 50.;
    TransitionPattern pattern = TransitionPattern::Automatic;
};

struct SelectionSettings {
    int tool = 0;
    double brush_radius = 2.;
    bool limit_edges = true;
    double edge_angle = 30.;
    double slope_min = 0.;
    double slope_max = 45.;
    double section_position = 0.;
};

struct Project {
    int version = schema_version;
    std::vector<Region> regions;
    std::array<Head, 3> heads;
    Strategy strategy = Strategy::AutomaticParallel;
    std::vector<Transition> transitions;
    SelectionSettings selection;

    Region *find(int id);
    const Region *find(int id) const;
    int next_id() const;
    std::string encode() const;
    // Empty is an ordinary Orca project. Unsupported/malformed data stays opaque in Model.
    static std::optional<Project> decode(const std::string &json, std::string &error);
};

// Triangle order/connectivity identity. Vertex motion/transforms retain surface ownership;
// topology replacement must not silently attach an old mask to unrelated triangles.
std::uint64_t topology_id(const indexed_triangle_set &mesh);
bool painting_matches(const ModelVolume &volume);
void remap_regions(Model &model, const std::vector<int> &sources, int destination);
void merge_regions(Project &project, Model &model, const std::vector<int> &ids, int destination);
void remove_regions(Project &project, Model &model, const std::vector<int> &ids);
}
}
