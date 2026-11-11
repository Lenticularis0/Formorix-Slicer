#include "FormorixPreviewView.hpp"
#include "FormorixHelp.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "Camera.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixUnderlay.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixMaterial.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include <libslic3r/FormorixPreview.hpp>
#include <memory>
#include <functional>
#include <vector>
#include <slic3r/GUI/GLModel.hpp>
#include <wx/event.h>
#include <wx/defs.h>
#include <wx/gdicmn.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>
#include <wx/wrapsizer.h>
#include <glad/gl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>
#include <map>
#include <imgui.h>
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace Slic3r::GUI {
namespace FP = FormorixPreview;
// GPU-only conversion. The Double audit mesh remains authoritative; a feature
// below Float display resolution must not generate nonfinite triangle normals.
static bool gpu_triangle(const FormorixGeometry::Mesh& mesh, const Vec3i32& face, std::array<Vec3f, 3>& vertices)
{
    for (int i = 0; i < 3; ++i) {
        if (face[i] < 0 || std::size_t(face[i]) >= mesh.vertices.size()) return false;
        const auto& p = mesh.vertices[face[i]];
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7) return false;
        vertices[i] = p.cast<float>();
    }
    return (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).squaredNorm() > 0.f;
}
static bool has_gpu_triangles(const FormorixGeometry::Mesh& mesh)
{
    std::array<Vec3f, 3> vertices;
    return std::any_of(mesh.faces.begin(), mesh.faces.end(), [&](const auto& face) { return gpu_triangle(mesh, face, vertices); });
}
static GLModel::Geometry gpu_geometry(const FormorixGeometry::Mesh& mesh, bool wireframe = false)
{
    GLModel::Geometry out;
    out.format = {wireframe ? GLModel::Geometry::EPrimitiveType::Lines : GLModel::Geometry::EPrimitiveType::Triangles,
                  GLModel::Geometry::EVertexLayout::P3};
    std::array<Vec3f, 3> vertices;
    for (const auto& face : mesh.faces) if (gpu_triangle(mesh, face, vertices)) {
        const unsigned first = unsigned(out.vertices_count());
        for (const auto& p : vertices) out.add_vertex(p);
        if (wireframe) { out.add_line(first, first + 1); out.add_line(first + 1, first + 2); out.add_line(first + 2, first); }
        else out.add_triangle(first, first + 1, first + 2);
    }
    return out;
}
wxString formorix_preview_error(FP::Issue issue)
{
    switch (issue) {
    case FP::Issue::Empty: return _L("Select a painted surface region.");
    case FP::Issue::InvalidMesh: return _L("Invalid mesh or preview parameters.");
    case FP::Issue::Disconnected: return _L("The selected region is not connected.");
    case FP::Issue::NonDisk: return _L("Use one region without holes or branching boundaries.");
    case FP::Issue::NotHeightField: return _L("This surface cannot be mapped to one raster patch.");
    case FP::Issue::ThinWall: return _L("Insufficient wall thickness or invalid inward offset.");
    case FP::Issue::InvalidOffset: return _L("The offset folds or intersects itself.");
    case FP::Issue::FullWallVariable: return _L("Full-wall preview requires nearly uniform local wall thickness.");
    case FP::Issue::VerticalDisabled: return _L("Enable vertical surfaces for this region.");
    case FP::Issue::TiltExceeded: return _L("Required nozzle tilt exceeds the allowed deviation.");
    case FP::Issue::TooLarge: return _L("Preview limit exceeded. Reduce the region or layer count.");
    case FP::Issue::Canceled: return _L("Preview calculation canceled.");
    case FP::Issue::StalePaint: return _L("Mesh topology changed. Select the surfaces again.");
    case FP::Issue::MultipleHeads: return _L("Assign exactly one enabled head for this preview.");
    case FP::Issue::ProfileMissing: return _L("The assigned material profile is missing.");
    case FP::Issue::ReferenceEdge: return _L("Reference edge is missing or ambiguous. Select a custom direction.");
    }
    return _L("Preview unavailable.");
}
static wxString state_name(FP::MotionKind kind)
{
    switch (kind) {
    case FP::MotionKind::Extrude: return _L("Working");
    case FP::MotionKind::Travel: return _L("Traveling (collision check pending)");
    case FP::MotionKind::Wait: return _L("Waiting");
    default: return _L("Unassigned");
    }
}
struct FormorixPreviewPanel::Impl : wxEvtHandler
{
    FormorixPreviewPanel& panel;
    std::shared_ptr<FormorixPreviewState> state = std::make_shared<FormorixPreviewState>();
    std::function<void()> changed;
    wxSlider* slider;
    wxStaticText *clock, *heads, *validation;
    ::ComboBox *layers, *speed;
    std::vector<Button*> buttons;
    std::vector<wxSizerItem*> hybrid_controls;
    std::array<::CheckBox*, 5> material_checks{};
    std::array<bool, 5> material_available{}; // computed once when a new immutable result arrives
    wxTimer timer;
    std::chrono::steady_clock::time_point last;
    double rate = 1.;
    Impl(FormorixPreviewPanel& owner, std::function<void()> on_change, std::function<void()> ordinary)
        : panel(owner), changed(std::move(on_change)), timer(this)
    {
        panel.SetBackgroundColour(wxGetApp().get_window_default_clr());
        panel.SetScrollRate(0, panel.FromDIP(10));
        auto* layout = new wxBoxSizer(wxVERTICAL);
        panel.SetSizer(layout);
        auto* row   = new wxBoxSizer(wxHORIZONTAL);
        auto action = [&](const wxString& name, std::function<void()> fn) {
            auto* b = new Button(&panel, name);
            b->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
            b->Bind(wxEVT_BUTTON, [fn](wxCommandEvent&) { fn(); });
            row->Add(b, 0, wxALL, panel.FromDIP(3));
            buttons.push_back(b);
        };
        action(_L("Start"), [this] {
            if (state->result && state->result->ok()) {
                if (state->seconds >= state->result->timeline.duration())
                    state->seconds = 0.;
                last = std::chrono::steady_clock::now();
                timer.Start(33);
            }
        });
        action(_L("Pause"), [this] { timer.Stop(); });
        action("|<", [this] {
            timer.Stop();
            seek(0.);
        });
        action("-1 s", [this] {
            timer.Stop();
            seek(state->seconds - 1.);
        });
        action("+1 s", [this] {
            timer.Stop();
            seek(state->seconds + 1.);
        });
        speed = new ::ComboBox(&panel, wxID_ANY, wxEmptyString, wxDefaultPosition, panel.FromDIP(wxSize(85, 30)), 0, nullptr, wxCB_READONLY);
        for (const char* s : {"0.25x", "1x", "4x", "16x"})
            speed->Append(s);
        speed->SetSelection(1);
        speed->SetToolTip(_L("Playback speed"));
        speed->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
            static const double rates[]{.25, 1., 4., 16.};
            rate = rates[std::clamp(speed->GetSelection(), 0, 3)];
        });
        row->Add(speed, 0, wxALL, panel.FromDIP(3));
        clock = new wxStaticText(&panel, wxID_ANY, wxEmptyString);
        row->Add(clock, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, panel.FromDIP(5));
        row->Add(formorix_help_button(&panel, FormorixHelpTopic::Preview), 0, wxALL, panel.FromDIP(3));
        layout->Add(row, 0, wxEXPAND);
        slider = new wxSlider(&panel, wxID_ANY, 0, 0, 10000, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
        slider->SetName(_L("5D process time"));
        slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
            timer.Stop();
            if (state->result)
                seek(state->result->timeline.duration() * slider->GetValue() / 10000.);
        });
        layout->Add(slider, 0, wxEXPAND | wxLEFT | wxRIGHT, panel.FromDIP(8));
        auto* options = new wxWrapSizer(wxHORIZONTAL);
        layers        = new ::ComboBox(&panel, wxID_ANY, wxEmptyString, wxDefaultPosition, panel.FromDIP(wxSize(170, 30)), 0, nullptr,
                                       wxCB_READONLY);
        layers->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
            state->layer = layers->GetSelection() - 1;
            changed();
        });
        options->Add(layers, 0, wxALL, panel.FromDIP(3));
        auto flag = [&](const wxString& name, bool FormorixPreviewState::* field, bool hybrid_only = false,
                        const wxString& tooltip = wxEmptyString) {
            auto* item = new wxBoxSizer(wxHORIZONTAL);
            auto* c = new ::CheckBox(&panel);
            c->SetName(name);
            c->SetValue(state.get()->*field);
            if (!tooltip.empty()) c->SetToolTip(tooltip);
            c->Bind(wxEVT_TOGGLEBUTTON, [this, c, field](wxCommandEvent& e) {
                state.get()->*field = c->GetValue();
                changed();
                e.Skip();
            });
            item->Add(c, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, panel.FromDIP(8));
            item->Add(new wxStaticText(&panel, wxID_ANY, name), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, panel.FromDIP(4));
            auto* entry = options->Add(item, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, panel.FromDIP(3));
            if (hybrid_only) hybrid_controls.push_back(entry);
            return c;
        };
        flag(_L("Original model"), &FormorixPreviewState::original, true);
        flag(_L("Remaining core"), &FormorixPreviewState::core);
        flag(_L("Orca core paths"), &FormorixPreviewState::core_paths, true);
        flag(_L("5D shell"), &FormorixPreviewState::shell);
        flag(_L("5D paths"), &FormorixPreviewState::surface_paths);
        flag(_L("Shared interfaces"), &FormorixPreviewState::interfaces, true);
        flag(_L("Missing underlay"), &FormorixPreviewState::missing_underlay, true);
        flag(_L("Unverified regions"), &FormorixPreviewState::unverified, true);
        const wxString all_layers = _L("All calculated layers; independent of playback time.");
        material_checks[0] = flag(_L("Nominal 3D material"), &FormorixPreviewState::material, true, all_layers);
        material_checks[1] = flag(_L("Missing material"), &FormorixPreviewState::missing_material, true, all_layers);
        material_checks[2] = flag(_L("Excess material"), &FormorixPreviewState::excess_material, true, all_layers);
        material_checks[3] = flag(_L("Material overlaps"), &FormorixPreviewState::material_overlap, true, all_layers);
        material_checks[4] = flag(_L("Diagnostic cells (unverified)"), &FormorixPreviewState::diagnostic_cells, true, all_layers);
        hybrid_controls.push_back(options->Add(formorix_help_button(&panel, FormorixHelpTopic::MaterialVolume),
                                               0, wxALL, panel.FromDIP(3)));
        auto* normal = new Button(&panel, _L("Orca preview"));
        normal->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        normal->Bind(wxEVT_BUTTON, [ordinary](wxCommandEvent&) { ordinary(); });
        options->Add(normal, 0, wxALL, panel.FromDIP(3));
        buttons.push_back(normal);
        auto* report = new Button(&panel, _L("Geometry report"));
        report->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        report->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_report(); });
        hybrid_controls.push_back(options->Add(report, 0, wxALL, panel.FromDIP(3)));
        buttons.push_back(report);
        layout->Add(options, 0, wxEXPAND);
        validation = new Label(&panel, wxEmptyString, LB_AUTO_WRAP);
        layout->Add(validation, 0, wxEXPAND | wxLEFT | wxRIGHT, panel.FromDIP(5));
        heads = new wxStaticText(&panel, wxID_ANY, wxEmptyString);
        heads->SetToolTip(_L("Positions are shown only for heads with calculated movements."));
        layout->Add(heads, 0, wxALL, panel.FromDIP(5));
        auto* scope = new Label(&panel,
            _L("Time: 5D part only. Orca core paths are not synchronized; supports and waiting times are excluded."), LB_AUTO_WRAP);
        layout->Add(scope, 0, wxEXPAND | wxLEFT | wxRIGHT, panel.FromDIP(5));
        auto* travel = new wxStaticText(&panel, wxID_ANY,
            _L("Layer-change travel: collision check pending."));
        layout->Add(travel, 0, wxLEFT | wxRIGHT, panel.FromDIP(5));
        Bind(
            wxEVT_TIMER,
            [this](wxTimerEvent&) {
                if (!panel.IsShownOnScreen() || wxGetApp().is_closing()) {
                    timer.Stop();
                    return;
                }
                const auto now     = std::chrono::steady_clock::now();
                const double delta = std::chrono::duration<double>(now - last).count();
                last               = now;
                seek(state->seconds + delta * rate);
            },
            timer.GetId());
        update();
    }
    ~Impl() override { timer.Stop(); }
    void show_report()
    {
        if (!state->hybrid) return;
        const auto& h = *state->hybrid;
        wxString text = _L("Numerical preview only. Physical accuracy, adhesion and collisions are unverified.");
        text += "\n\n" + format_wxstr(_L("Original volume: %1% mm³; partition residual: %2% mm³."),
            wxString::Format("%.9g", h.partition.input_volume), wxString::Format("%.9g", h.partition.volume_residual));
        if (!h.error.empty()) text += "\n" + from_u8(h.error);
        const auto number = [](double value) { return std::isfinite(value) ? wxString::Format("%.9g", value) : _L("Unknown"); };
        auto distance = [&](const wxString& name, const FormorixMaterial::Distance& value) {
            text += "\n" + format_wxstr(_L("%1%: sampled maximum / mean / RMS = %2% / %3% / %4% mm; conservative upper bound = %5% mm; observed lower bound = %6% mm; samples: %7%."),
                name, number(value.maximum), number(value.mean), number(value.rms),
                value.bounded ? number(value.upper_bound) : _L("Unknown"),
                value.bounded || value.samples > 0 ? number(value.lower_bound) : _L("Unknown"), value.samples);
        };
        const auto& material = h.material;
        if (!material.ok()) {
            text += "\n\n" + _L("Spatial material audit incomplete. Volumes, missing material and overlaps are unknown; raw diagnostic cells may be incomplete.");
            if (!material.error.empty()) text += "\n" + from_u8(material.error);
        } else {
            text += "\n\n" + format_wxstr(_L("All-layer 3D material (mm³), supplied / cells / union / missing / outside / duplicate / core overlap: %1% / %2% / %3% / %4% / %5% / %6% / %7%."),
                number(material.supplied_volume), number(material.cell_volume), number(material.union_volume),
                number(material.missing_volume), number(material.outside_volume), number(material.duplicate_volume),
                number(material.core_overlap_volume));
            text += "\n" + format_wxstr(_L("Supply minus cell volume: %1% mm³; spatial output rounding allowance: %2% mm; audit time: %3% s."),
                number(material.supply_geometry_residual), number(material.rounding_bound), number(material.elapsed_seconds));
            distance(_L("Complete volume boundaries"), material.boundary_distance);
            distance(_L("Nominal outer-layer sheets"), material.nominal_outer_distance);
            text += "\n" + _L("Outer-layer sheets alone do not prove the complete material envelope. Same-material duplicate volume is measured but has no separate display mesh.");
            for (const auto& overlap : material.material_overlaps)
                text += "\n" + format_wxstr(_L("Overlap between materials %1% and %2%: %3% mm³."),
                    from_u8(overlap.first), from_u8(overlap.second), number(overlap.volume));
        }
        text += "\n" + _L("The GPU display omits triangles below its numerical resolution. Numerical audit values remain authoritative.");
        for (const auto& r : h.regions) {
            text += "\n\n" + format_wxstr(_L("Region %1%, head %2%: nominal volume / shell volume = %3%."),
                r.id, r.head + 1, wxString::Format("%.4g", r.volume_ratio));
            text += "\n" + format_wxstr(_L("Deposition model: %1%. Derived compacted width: %2% mm; supplied cross-section: %3% mm². Material deformation is not calibrated."),
                from_u8(r.deposition_model), wxString::Format("%.6g", r.compacted_width), wxString::Format("%.6g", r.supplied_area));
            if (r.inadmissible_material)
                text += "\n" + format_wxstr(_L("Inadmissible material excess: %1% mm³. This region is diagnostic only; flow was not corrected."),
                    wxString::Format("%.6g", r.excess_material_volume_mm3));
            text += "\n" + format_wxstr(_L("Material %1%; nozzle %2% mm; line width %3% mm; layer height %4% mm; temperature %5% °C; volumetric limit %6% mm³/s; speed %7% mm/s."),
                from_u8(r.process.material_id), wxString::Format("%.4g", r.process.nozzle_diameter),
                wxString::Format("%.4g", r.process.line_width), wxString::Format("%.4g", r.process.layer_height),
                wxString::Format("%.4g", r.process.temperature), wxString::Format("%.4g", r.process.volumetric_limit),
                wxString::Format("%.4g", r.process.speed));
            text += "\n" + format_wxstr(_L("Configured flow factors, filament / process / surface: %1% / %2% / %3%; local requested line width: %4%–%5% mm."),
                number(r.process.flow_ratio), number(r.process.print_flow_ratio), number(r.process.surface_flow_ratio),
                number(r.minimum_local_width), number(r.maximum_local_width));
            if (material.ok()) {
                const auto found = std::find_if(material.regions.begin(), material.regions.end(), [&](const auto& region) { return region.region == r.id; });
                if (found != material.regions.end()) {
                    text += "\n" + format_wxstr(_L("Regional 3D volumes (mm³), target / deposited / missing / outside / duplicate / core overlap: %1% / %2% / %3% / %4% / %5% / %6%."),
                        number(found->target_volume), number(found->deposited_volume), number(found->missing_volume),
                        number(found->outside_volume), number(found->duplicate_volume), number(found->core_overlap_volume));
                    distance(_L("Regional volume boundaries"), found->boundary_distance);
                    text += "\n" + (r.accuracy.verified() ?
                        _L("Nominal regional CAD error budget ≤ 0.01 mm. Physical accuracy and conventional deposited surfaces remain unverified.") :
                        _L("Regional CAD error budget ≤ 0.01 mm is not verified."));
                }
            }
            text += "\n" + format_wxstr(_L("Coverage samples: %1%; uncovered fraction: %2%. Sample error max / mean / RMS: %3% / %4% / %5% mm."),
                r.coverage.samples, wxString::Format("%.3g", r.coverage.uncovered_fraction),
                wxString::Format("%.6g", r.coverage.sampled_maximum), wxString::Format("%.6g", r.coverage.sampled_mean),
                wxString::Format("%.6g", r.coverage.sampled_rms));
            text += "\n" + format_wxstr(_L("Nominal footprint outside the selected region: %1%; sampled overlap into the core: %2% mm. This may overlap conventional material."),
                wxString::Format("%.6g", r.coverage.outside_fraction), wxString::Format("%.6g", r.nominal_core_overlap));
            text += "\n" + format_wxstr(_L("Projected ribbon volumes (mm³), union / uncovered / outside / overlap: %1% / %2% / %3% / %4%. Curved 3D coverage requires separate verification."),
                wxString::Format("%.6g", r.projected_union_volume), wxString::Format("%.6g", r.projected_uncovered_volume),
                wxString::Format("%.6g", r.projected_outside_volume), wxString::Format("%.6g", r.projected_overlap_volume));
            text += "\n" + format_wxstr(_L("Error bounds (mm), CAD / shell / conversion / path / nominal material: %1% / %2% / %3% / %4% / %5%. Infinity means unverified."),
                wxString::Format("%.6g", r.accuracy.bounds[0]), wxString::Format("%.6g", r.accuracy.bounds[1]),
                wxString::Format("%.6g", r.accuracy.bounds[2]), wxString::Format("%.6g", r.accuracy.bounds[3]), wxString::Format("%.6g", r.accuracy.bounds[4]));
            text += "\n" + format_wxstr(_L("Conservative combined error bound: %1% mm. Unknown CAD input keeps the total unverified."),
                number(r.accuracy.total_bound()));
            text += "\n" + format_wxstr(_L("Underlay: contact unconfirmed at %1% of %2% samples; maximum gap: %3% mm. Full footprint and material adhesion remain unverified."),
                r.underlay.missing, r.underlay.samples, wxString::Format("%.6g", r.underlay.maximum_gap));
            text += "\n" + format_wxstr(_L("Underlay markers: red = no conventional core (%1%); orange = excluded infill (%2%); magenta = incomplete path coverage (%3%); gray = unknown geometry (%4%)."),
                r.underlay.missing_core, r.underlay.unsuitable_infill, r.underlay.path_gaps, r.underlay.unknown_geometry);
            text += "\n" + format_wxstr(_L("Largest observed contact gap: %1% mm; samples without eligible contact within 0.5 mm: %2%. This is not a global distance bound."),
                wxString::Format("%.6g", r.underlay.maximum_finite_gap), r.underlay.no_nearby_contact);
            text += "\n" + format_wxstr(_L("Raw native contact not demonstrated: %1% samples; native-layer gaps: %2%. Unsupported cross-sections are included in the first count; material suitability remains unverified."),
                r.underlay.raw_missing, r.underlay.native_layer_gaps);
        }
        for (const auto& warning : h.warnings) text += "\n" + from_u8(warning);
        if (state->underlay_preparation && !state->underlay_preparation->ok())
            text += "\n" + from_u8(state->underlay_preparation->error);
        auto underlay_report = [&](const FormorixUnderlay::Analysis& analysis, const wxString& label) {
            text += "\n\n" + label;
            if (!analysis.ok()) text += "\n" + from_u8(analysis.error);
            for (const auto& region : analysis.regions) {
                text += "\n" + format_wxstr(_L("Region %1%: sampled interface area %2% mm²; raw missing / native-layer gaps / path gaps / unknown: %3% / %4% / %5% / %6% mm². Area-weighted samples, not a complete contact certificate."),
                    region.region, wxString::Format("%.6g", region.interface_area),
                    wxString::Format("%.6g", region.raw_missing_area), wxString::Format("%.6g", region.native_layer_gap_area),
                    wxString::Format("%.6g", region.path_gap_area), wxString::Format("%.6g", region.unknown_area));
                if (region.external_support_required)
                    text += "\n" + _L("No core interface: an external underlay operation is required and remains unassigned.");
            }
            for (const auto& warning : analysis.warnings) text += "\n" + from_u8(warning);
        };
        if (state->underlay_before) underlay_report(*state->underlay_before, _L("Underlay before local Orca refinement"));
        if (state->underlay_after) underlay_report(*state->underlay_after, _L("Underlay after local Orca refinement"));
        if (state->conventional) {
            const auto& c = *state->conventional;
            if (!c.ok())
                text += "\n" + format_wxstr(_L("Orca core preview failed: %1%"), from_u8(c.error));
            else {
                text += "\n\n" + format_wxstr(_L("Orca conversion bound: %1% mm; coordinate quantization: %2% mm; volume residual: %3% mm³."),
                    wxString::Format("%.9g", c.float_conversion_bound), wxString::Format("%.9g", c.coordinate_quantization_bound),
                    wxString::Format("%.9g", c.converted_volume_residual));
                text += "\n" + format_wxstr(_L("Native underlay requests applied: %1%; modifier volumes: %2%; modifier conversion bound: %3% mm. Manufacturing dependencies remain pending."),
                    c.dense_requests_applied, c.dense_modifier_volumes, wxString::Format("%.9g", c.modifier_float_conversion_bound));
                text += "\n" + format_wxstr(_L("Native slice safety expansion bound: %1% mm. Original core geometry is unchanged."),
                    wxString::Format("%.9g", c.native_slice_expansion_bound));
                text += "\n" + format_wxstr(_L("Requested / applied density area summed over native layers: %1% / %2% mm²; modifier offset allowance: %3% mm. This does not bound the distance across removed thin mask features."),
                    wxString::Format("%.9g", c.dense_requested_area), wxString::Format("%.9g", c.dense_applied_area),
                    wxString::Format("%.9g", c.modifier_boundary_retreat_bound));
            }
            for (const auto& warning : c.warnings) text += "\n" + from_u8(warning);
        }
        show_info(&panel, text, _L("Hybrid geometry report"));
    }
    void seek(double t)
    {
        state->seconds = std::clamp(t, 0., state->result ? state->result->timeline.duration() : 0.);
        if (!state->result || state->seconds >= state->result->timeline.duration())
            timer.Stop();
        update();
        changed();
    }
    void update()
    {
        for (auto* control : hybrid_controls) control->Show(bool(state->hybrid));
        for (std::size_t i = 0; i < material_available.size(); ++i) material_checks[i]->Enable(material_available[i]);
        const double total = state->result ? state->result->timeline.duration() : 0.;
        slider->SetValue(total > 0. ? int(10000. * state->seconds / total) : 0);
        slider->Enable(total > 0.);
        clock->SetLabel(wxString::Format(_L("5D: %1$.1f / %2$.1f s (estimate)"), state->seconds, total));
        wxString text;
        for (int h = 0; h < 3; ++h) {
            if (h)
                text += "  |  ";
            text += format_wxstr(_L("Head %1%: %2%"), h + 1,
                                 state_name(state->result ? state->result->timeline.sample(h, state->seconds).kind :
                                                            FP::MotionKind::Unassigned));
        }
        if (state->result && !state->result->errors.empty())
            text = formorix_preview_error(state->result->errors.front());
        if (!state->result)
            text = _L("Model or settings changed. Calculate the preview again.");
        heads->SetLabel(text);
        wxString assessment;
        if (state->hybrid) {
            const auto& h = *state->hybrid;
            if (!h.error.empty()) assessment = format_wxstr(_L("Hybrid geometry failed: %1%"), from_u8(h.error));
            else if (state->conventional && !state->conventional->error.empty())
                assessment = format_wxstr(_L("Orca core preview failed: %1%"), from_u8(state->conventional->error));
            else {
                const bool plausible = std::all_of(h.regions.begin(), h.regions.end(), [](const auto& r) { return r.accuracy.material_volume_plausible; });
                const bool inadmissible = std::any_of(h.regions.begin(), h.regions.end(), [](const auto& r) { return r.inadmissible_material; });
                assessment = inadmissible ? _L("Material excess is inadmissible. Diagnostic preview only; flow unchanged.") :
                    plausible ? _L("Nominal material volume plausible. CAD accuracy and full surface coverage: see geometry report.") :
                    _L("Nominal material volume implausible; flow unchanged. CAD accuracy and full surface coverage are unverified.");
                if (!h.material.ok())
                    assessment = _L("Spatial material audit incomplete. Accepted 3D volumes are unavailable; diagnostic cells remain unverified.");
                if (!h.missing_underlay.empty()) assessment += " " + _L("Underlay contact is missing or unverified at sampled points.");
                if (state->underlay_before && state->underlay_after && state->underlay_after->ok()) {
                    const auto& before = state->underlay_before->regions;
                    const auto& after = state->underlay_after->regions;
                    if (before.size() == after.size())
                        for (std::size_t i = 0; i < before.size(); ++i)
                            if (after[i].unknown_area > before[i].unknown_area) {
                                assessment += " " + _L("Local density creates unverified cross-sections. See the geometry report.");
                                break;
                            }
                }
                if ((state->underlay_preparation && !state->underlay_preparation->ok()) ||
                    (state->underlay_before && !state->underlay_before->ok()) ||
                    (state->underlay_after && !state->underlay_after->ok()))
                    assessment += " " + _L("Underlay verification is incomplete. See the geometry report.");
            }
        }
        validation->SetLabel(assessment);
        panel.Layout();
        panel.FitInside();
    }
};
FormorixPreviewPanel::FormorixPreviewPanel(wxWindow* p, std::function<void()> changed, std::function<void()> ordinary)
    : wxScrolledWindow(p), m_impl(std::make_unique<Impl>(*this, std::move(changed), std::move(ordinary)))
{}
FormorixPreviewPanel::~FormorixPreviewPanel() = default;
std::shared_ptr<FormorixPreviewState> FormorixPreviewPanel::state() const { return m_impl->state; }
void FormorixPreviewPanel::pause() { m_impl->timer.Stop(); }
void FormorixPreviewPanel::set_result(std::shared_ptr<const FP::Result> result)
{
    pause();
    auto& p          = *m_impl;
    p.state->result  = std::move(result);
    p.state->hybrid.reset();
    p.state->conventional.reset();
    p.state->underlay_preparation.reset();
    p.state->underlay_before.reset();
    p.state->underlay_after.reset();
    p.material_available.fill(false);
    p.state->seconds = 0.;
    p.state->layer   = -1;
    p.layers->Clear();
    p.layers->Append(_L("All 5D layers"));
    if (p.state->result)
        for (std::size_t i = 0; i < p.state->result->layers.size(); ++i)
            p.layers->Append(format_wxstr(_L("5D layer %1%"), i + 1));
    p.layers->SetSelection(0);
    p.update();
    p.changed();
}
void FormorixPreviewPanel::set_hybrid(std::shared_ptr<const FormorixHybrid::Result> hybrid,
                                    std::shared_ptr<const FormorixCorePreview::Result> conventional,
                                    std::shared_ptr<const FormorixUnderlay::Preparation> preparation,
                                    std::shared_ptr<const FormorixUnderlay::Analysis> before,
                                    std::shared_ptr<const FormorixUnderlay::Analysis> after)
{
    if (!hybrid) { set_result({}); return; }
    set_result(std::shared_ptr<const FP::Result>(hybrid, &hybrid->preview));
    m_impl->state->hybrid = std::move(hybrid);
    m_impl->state->conventional = std::move(conventional);
    m_impl->state->underlay_preparation = std::move(preparation);
    m_impl->state->underlay_before = std::move(before);
    m_impl->state->underlay_after = std::move(after);
    const auto& material = m_impl->state->hybrid->material;
    m_impl->material_available = {{material.ok() && has_gpu_triangles(material.deposited),
        material.ok() && has_gpu_triangles(material.missing), material.ok() && has_gpu_triangles(material.outside),
        material.ok() && (has_gpu_triangles(material.core_overlap) ||
            std::any_of(material.material_overlaps.begin(), material.material_overlaps.end(),
                        [](const auto& overlap) { return has_gpu_triangles(overlap.mesh); })),
        has_gpu_triangles(material.diagnostic_cells)}};
    m_impl->update();
    m_impl->changed();
}
void FormorixPreviewPanel::rescale()
{
    for (auto* b : m_impl->buttons)
        b->Rescale();
    m_impl->layers->Rescale();
    m_impl->speed->Rescale();
    Layout();
}
void FormorixPreviewPanel::theme()
{
    SetBackgroundColour(wxGetApp().get_window_default_clr());
    wxGetApp().UpdateDarkUIWin(this);
    Refresh();
}

static GLModel::Geometry lines(const std::vector<FP::Pose>& path)
{
    GLModel::Geometry g;
    g.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    for (const auto& p : path)
        g.add_vertex(Vec3f(p.position.cast<float>()));
    for (unsigned i = 1; i < path.size(); ++i)
        g.add_line(i - 1, i);
    return g;
}
void FormorixPreviewRenderer::rebuild(const FP::Result& r)
{
    m_paths.clear();
    m_shells.clear();
    m_core.reset();
    m_invalid.reset();
    m_active.reset();
    m_axis.reset();
    for (const auto& layer : r.layers) {
        auto mesh = std::make_unique<GLModel>();
        mesh->init_from(layer.surface);
        m_shells.push_back(std::move(mesh));
    }
    // Rendering consumes the three independent timed tracks, not the single-head planner's paths.
    // Future scheduling can supply these same tracks without changing the preview.
    for (int h = 0; h < 3; ++h) {
        std::map<int, std::vector<const FP::Motion*>> layers;
        for (const auto& m : r.timeline.heads[h])
            if (m.extrusion)
                layers[m.layer].push_back(&m);
        for (const auto& [layer, segments] : layers) {
            auto display   = std::make_unique<PathDisplay>();
            display->head  = h;
            display->layer = layer;
            GLModel::Geometry path, arrows;
            path.format   = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
            arrows.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
            std::vector<std::size_t> center_segments;
            if (layer >= 0 && std::size_t(layer) < r.layers.size()) {
                const auto& source = r.layers[layer];
                if (source.extruding.empty() || source.extruding.size() + 1 == source.path.size())
                    for (std::size_t i = 1; i < source.path.size(); ++i)
                        if (source.extruding.empty() || source.extruding[i - 1])
                            center_segments.push_back(i - 1);
            }
            const bool centers = center_segments.size() == segments.size();
            for (std::size_t j = 0; j < segments.size(); ++j) {
                const auto& m = *segments[j];
                const Vec3d from = centers ? r.layers[layer].path[center_segments[j]].position : m.from.position;
                const Vec3d to = centers ? r.layers[layer].path[center_segments[j] + 1].position : m.to.position;
                unsigned base = unsigned(path.vertices_count());
                path.add_vertex(Vec3f(from.cast<float>()));
                path.add_vertex(Vec3f(to.cast<float>()));
                path.add_line(base, base + 1);
                display->ends.push_back(m.end);
                if (j % std::max(std::size_t(1), segments.size() / 12) == 0) {
                    Vec3d d = to - from;
                    if (d.norm() < 1e-8)
                        continue;
                    d.normalize();
                    Vec3d side = d.cross(m.to.axis);
                    if (side.norm() < 1e-8)
                        continue;
                    side.normalize();
                    base          = unsigned(arrows.vertices_count());
                    const auto& p = to;
                    arrows.add_vertex(Vec3f((p + .55 * d).cast<float>()));
                    arrows.add_vertex(Vec3f((p - .35 * d + .25 * side).cast<float>()));
                    arrows.add_vertex(Vec3f((p - .35 * d - .25 * side).cast<float>()));
                    arrows.add_triangle(base, base + 1, base + 2);
                }
            }
            display->path.init_from(std::move(path));
            if (!arrows.is_empty())
                display->arrows.init_from(std::move(arrows));
            display->start  = centers ? r.layers[layer].path[center_segments.front()].position : segments.front()->from.position;
            display->finish = centers ? r.layers[layer].path[center_segments.back() + 1].position : segments.back()->to.position;
            m_paths.push_back(std::move(display));
        }
    }
    if (!r.core.indices.empty())
        m_core.init_from(r.core);
    if (!r.ok() && !r.selected.indices.empty())
        m_invalid.init_from(r.selected);
    if (!m_marker.is_initialized())
        m_marker.init_from(smooth_sphere(12, .45f));
}
void FormorixPreviewRenderer::rebuild_hybrid(const FormorixPreviewState& s)
{
    m_original.reset(); m_interfaces.reset(); m_missing_underlay.reset(); m_unverified.reset();
    m_unsuitable_infill.reset(); m_underlay_gaps.reset(); m_unknown_underlay.reset();
    m_native_layer_gaps.reset();
    m_material.reset(); m_material_missing.reset(); m_material_outside.reset(); m_material_core_overlap.reset();
    m_diagnostic_cells.reset(); m_material_overlaps.clear();
    m_core_paths.clear();
    if (!s.hybrid) return;
    const auto& h = *s.hybrid;
    auto mesh = [](GLModel& model, const FormorixGeometry::Mesh& source) {
        model.reset();
        auto geometry = gpu_geometry(source);
        if (!geometry.is_empty()) model.init_from(std::move(geometry));
    };
    mesh(m_original, h.original);
    mesh(m_core, h.core);
    // These are the exact audit's output meshes, converted only for GPU display.
    // Failed audits may expose individual cells, never accepted union evidence.
    auto cells = gpu_geometry(h.material.diagnostic_cells, true);
    if (!cells.is_empty()) m_diagnostic_cells.init_from(std::move(cells));
    if (h.material.ok()) {
        mesh(m_material, h.material.deposited);
        mesh(m_material_missing, h.material.missing);
        mesh(m_material_outside, h.material.outside);
        mesh(m_material_core_overlap, h.material.core_overlap);
        for (const auto& overlap : h.material.material_overlaps) {
            auto display = std::make_unique<GLModel>();
            mesh(*display, overlap.mesh);
            m_material_overlaps.push_back(std::move(display));
        }
    }
    m_shells.clear();
    for (const auto& part : h.partition.parts) if (part.region != 0) {
        auto display = std::make_unique<GLModel>();
        mesh(*display, part.mesh);
        m_shells.push_back(std::move(display));
    }
    GLModel::Geometry interfaces;
    interfaces.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
    for (const auto& boundary : h.partition.interfaces) {
        const unsigned first = unsigned(interfaces.vertices_count());
        for (const auto& p : boundary.mesh.vertices) interfaces.add_vertex(Vec3f(p.cast<float>()));
        for (const auto& f : boundary.mesh.faces) interfaces.add_triangle(first + f[0], first + f[1], first + f[2]);
    }
    if (!interfaces.is_empty()) m_interfaces.init_from(std::move(interfaces));
    auto points = [](GLModel& model, const std::vector<Vec3d>& source) {
        GLModel::Geometry g;
        g.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        for (const auto& p : source) for (int axis = 0; axis < 3; ++axis) {
            const unsigned first = unsigned(g.vertices_count());
            const Vec3d delta = .22 * Vec3d::Unit(axis);
            g.add_vertex(Vec3f((p - delta).cast<float>())); g.add_vertex(Vec3f((p + delta).cast<float>()));
            g.add_line(first, first + 1);
        }
        if (!g.is_empty()) model.init_from(std::move(g));
    };
    points(m_missing_underlay, h.missing_core);
    points(m_unsuitable_infill, h.unsuitable_infill);
    points(m_underlay_gaps, h.path_gaps);
    points(m_unknown_underlay, h.unknown_underlay);
    points(m_native_layer_gaps, h.native_layer_gaps);
    points(m_unverified, h.unverified);
    if (s.conventional) for (int head = 0; head < 3; ++head) {
        GLModel::Geometry g;
        g.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        for (const auto& path : s.conventional->paths) if (path.head == head) {
            const unsigned first = unsigned(g.vertices_count());
            for (const auto& p : path.points) g.add_vertex(Vec3f(p.cast<float>()));
            for (std::size_t i = 1; i < path.points.size(); ++i) g.add_line(first + unsigned(i - 1), first + unsigned(i));
        }
        auto display = std::make_unique<GLModel>();
        if (!g.is_empty()) display->init_from(std::move(g));
        m_core_paths.push_back(std::move(display));
    }
}
void FormorixPreviewRenderer::render(const FormorixPreviewState& s, const Camera& camera)
{
    if (!s.result)
        return;
    if (m_result != s.result) {
        m_result = s.result;
        rebuild(*m_result);
    }
    if (m_hybrid != s.hybrid || m_conventional != s.conventional) {
        m_hybrid = s.hybrid;
        m_conventional = s.conventional;
        rebuild_hybrid(s);
    }
    auto* shader = wxGetApp().get_shader("flat");
    if (!shader)
        return;
    shader->start_using();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (s.original && s.hybrid) {
        glDepthMask(GL_FALSE);
        m_original.set_color(ColorRGBA(.55f, .6f, .65f, .18f));
        m_original.render();
        glDepthMask(GL_TRUE);
    }
    if (s.core) {
        m_core.set_color(ColorRGBA(.55f, .57f, .6f, .35f));
        m_core.render();
    }
    if (s.shell) {
        glDepthMask(GL_FALSE);
        for (std::size_t l = 0; l < m_shells.size(); ++l) {
            if (!s.hybrid && s.layer >= 0 && int(l) != s.layer)
                continue;
            auto& mesh = *m_shells[l];
            mesh.set_color(ColorRGBA(.2f, .75f, .85f, .12f));
            mesh.render();
        }
        glDepthMask(GL_TRUE);
    }
    if (s.interfaces) {
        glDepthMask(GL_FALSE);
        m_interfaces.set_color(ColorRGBA(.85f, .25f, .9f, .5f));
        m_interfaces.render();
        glDepthMask(GL_TRUE);
    }
    if (s.hybrid) {
        glDepthMask(GL_FALSE);
        auto material_mesh = [](GLModel& mesh, const ColorRGBA& color) {
            mesh.set_color(color);
            mesh.render();
        };
        if (s.material) material_mesh(m_material, ColorRGBA(.5f, .7f, .75f, .45f));
        if (s.missing_material) material_mesh(m_material_missing, ColorRGBA(1.f, .15f, .2f, .8f));
        if (s.excess_material) material_mesh(m_material_outside, ColorRGBA(1.f, .6f, .05f, .8f));
        if (s.material_overlap) {
            material_mesh(m_material_core_overlap, ColorRGBA(.75f, .15f, 1.f, .8f));
            for (const auto& overlap : m_material_overlaps)
                material_mesh(*overlap, ColorRGBA(.95f, .1f, .7f, .8f));
        }
        if (s.diagnostic_cells) material_mesh(m_diagnostic_cells, ColorRGBA(.8f, .65f, .35f, .2f));
        glDepthMask(GL_TRUE);
    }
    m_invalid.set_color(ColorRGBA(1.f, .15f, .1f, .7f));
    m_invalid.render();
    // Lines are a preview overlay, so inner layers remain visible through the context mesh.
    glDisable(GL_DEPTH_TEST);
    if (s.core_paths) {
        const std::array<ColorRGBA, 3> colors{{{.15f,.5f,1.f,1.f}, {.15f,.8f,.9f,1.f}, {.55f,.35f,1.f,1.f}}};
        for (std::size_t i = 0; i < m_core_paths.size(); ++i) {
            m_core_paths[i]->set_color(colors[i % colors.size()]);
            m_core_paths[i]->render();
        }
    }
    if (s.missing_underlay) {
        m_missing_underlay.set_color(ColorRGBA(1.f, .1f, .12f, 1.f));
        m_missing_underlay.render();
        m_unsuitable_infill.set_color(ColorRGBA(1.f, .55f, .05f, 1.f));
        m_unsuitable_infill.render();
        m_underlay_gaps.set_color(ColorRGBA(.95f, .15f, .9f, 1.f));
        m_underlay_gaps.render();
        m_unknown_underlay.set_color(ColorRGBA(.6f, .6f, .6f, 1.f));
        m_unknown_underlay.render();
        m_native_layer_gaps.set_color(ColorRGBA(.45f, .25f, .05f, 1.f));
        m_native_layer_gaps.render();
    }
    if (s.unverified) {
        m_unverified.set_color(ColorRGBA(1.f, .35f, .85f, 1.f));
        m_unverified.render();
    }
    for (auto& display : m_paths) {
        if (!s.surface_paths) continue;
        if (s.layer >= 0 && display->layer != s.layer)
            continue;
        auto& model = display->path;
        model.set_color(ColorRGBA(.9f, .65f, .15f, 1.f));
        model.render();
        const std::size_t completed = std::size_t(std::upper_bound(display->ends.begin(), display->ends.end(), s.seconds) -
                                                  display->ends.begin());
        if (completed) {
            model.set_color(ColorRGBA(.05f, .75f, .55f, 1.f));
            model.render({0, std::min(completed * 2, model.indices_count())});
        }
    }
    for (auto& display : m_paths)
        if (s.surface_paths && (s.layer < 0 || display->layer == s.layer)) {
            display->arrows.set_color(wxGetApp().dark_mode() ? ColorRGBA(.9f, .9f, .9f, 1.f) : ColorRGBA(.15f, .2f, .25f, 1.f));
            display->arrows.render();
        }
    auto marker = [&](const Vec3d& p, const ColorRGBA& color) {
        Transform3d transform = Transform3d::Identity();
        transform.translate(p);
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * transform);
        m_marker.set_color(color);
        m_marker.render();
    };
    if (s.result->ok())
        for (int h = 0; h < 3; ++h) {
            const auto state      = s.result->timeline.sample(h, s.seconds);
            if (!state.pose_known) continue;
            const ColorRGBA color = state.kind == FP::MotionKind::Extrude ? ColorRGBA(.1f, 1.f, .4f, 1.f) :
                                    state.kind == FP::MotionKind::Travel  ? ColorRGBA(1.f, .5f, .1f, 1.f) :
                                                                            ColorRGBA(.65f, .65f, .75f, 1.f);
            marker(state.pose.position, color);
            // Number each tip in screen space; the marker and short axis remain true 3D geometry.
            const auto& p              = state.pose.position;
            const Eigen::Vector4d clip = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix() *
                                         Eigen::Vector4d(p.x(), p.y(), p.z(), 1.);
            if (clip.w() > 0. && ImGui::GetCurrentContext()) {
                const auto& viewport = camera.get_viewport();
                const double x = clip.x() / clip.w(), y = clip.y() / clip.w();
                if (std::abs(x) < 1. && std::abs(y) < 1.) {
                    const char* labels[]{"1", "2", "3"};
                    const ImVec2 pos(float((x + 1.) * viewport[2] * .5 + 6.), float((1. - y) * viewport[3] * .5 - 10.));
                    const ImVec2 size = ImGui::CalcTextSize(labels[h]);
                    auto* draw        = ImGui::GetForegroundDrawList();
                    draw->AddRectFilled(ImVec2(pos.x - 2, pos.y - 1), ImVec2(pos.x + size.x + 2, pos.y + size.y + 1),
                                        IM_COL32(25, 30, 35, 220), 2.f);
                    draw->AddText(pos, IM_COL32(255, 255, 255, 255), labels[h]);
                }
            }
            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            m_axis.reset();
            m_axis.init_from(lines({state.pose, {state.pose.position + state.pose.axis * 2.5, state.pose.axis}}));
            m_axis.set_color(color);
            m_axis.render();
            const auto& track = s.result->timeline.heads[h];
            if (s.surface_paths && state.segment < track.size() && state.kind != FP::MotionKind::Wait &&
                (s.layer < 0 || track[state.segment].layer == s.layer)) {
                m_active.reset();
                m_active.init_from(lines({track[state.segment].from, track[state.segment].to}));
                m_active.set_color(state.kind == FP::MotionKind::Travel ? ColorRGBA(1.f, .5f, .1f, 1.f) :
                                                                         ColorRGBA(1.f, 1.f, 1.f, 1.f));
                m_active.render();
            }
        }
    for (const auto& display : m_paths) {
        if (!s.surface_paths) continue;
        if (s.layer >= 0 && display->layer != s.layer)
            continue;
        marker(display->start, ColorRGBA(.1f, 1.f, .1f, 1.f));
        marker(display->finish, ColorRGBA(1.f, .15f, .2f, 1.f));
    }
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    shader->stop_using();
}
} // namespace Slic3r::GUI
