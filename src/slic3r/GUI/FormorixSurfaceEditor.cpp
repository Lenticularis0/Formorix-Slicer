#include "FormorixSurfaceEditor.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include <cstddef>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <exception>
#include <memory>

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "Tab.hpp"
#include "OptionsGroup.hpp"
#include "Gizmos/GLGizmosManager.hpp"
#include "Gizmos/GLGizmoFormorixSurfaces.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include <wx/collpane.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>
#include <wx/arrstr.h>
#include <boost/any.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI {
namespace FS = FormorixSurfaces;

struct FormorixSurfaceEditor::Impl : wxEvtHandler {
    Plater &plater;
    wxScrolledWindow *left;
    wxScrolledWindow *right;
    wxListBox *regions = nullptr;
    ::ComboBox *objects = nullptr;
    ::ComboBox *transition_target = nullptr;
    wxStaticText *status = nullptr;
    wxStaticText *continuity = nullptr;
    wxStaticText *wall_warning = nullptr;
    wxStaticText *head_status = nullptr;
    wxStaticText *edge_status = nullptr;
    std::array<wxStaticText *, 3> assignments{};
    std::array<::ComboBox *, 3> materials{};
    std::vector<std::string> material_names;
    std::vector<std::function<void()>> refreshers;
    std::vector<wxWindow *> region_controls;
    std::vector<wxWindow *> project_controls;
    std::vector<std::pair<wxWindow *, std::function<bool()>>> dependencies;
    std::vector<std::pair<wxWindow *, std::function<bool()>>> pending_edits;
    std::vector<int> selected;
    std::vector<int> neighbor_ids;
    FS::Project project;
    FS::Settings empty_settings;
    FS::Transition empty_transition;
    int active = 0;
    int neighbor = 0;
    bool shown = false;
    bool updating = false;
    bool writable = true;
    bool high_speed_ready = false;
    std::string cached_json;
    std::string object_signature;
    std::string support_signature;
    wxTimer timer;
    DynamicPrintConfig support_config;
    std::unique_ptr<ConfigOptionsGroup> support_group;

    explicit Impl(Plater &owner) : plater(owner), timer(this)
    {
        left = new wxScrolledWindow(&plater, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxCLIP_CHILDREN);
        right = new wxScrolledWindow(&plater, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxCLIP_CHILDREN);
        for (auto *pane : {left, right}) {
            pane->SetBackgroundColour(wxGetApp().get_window_default_clr());
            pane->SetSizer(new wxBoxSizer(wxVERTICAL));
            pane->SetScrollRate(0, pane->FromDIP(12));
        }
        build_left();
        build_right();
        Bind(wxEVT_TIMER, [this](wxTimerEvent &) { if (shown) sync(); });
        theme();
    }
    ~Impl() override { timer.Stop(); }

    FS::Region *region() { return project.find(active); }
    FS::Settings &settings() { auto *r = region(); return r ? r->settings : empty_settings; }
    FS::Transition &transition()
    {
        const auto pair = std::minmax(active, neighbor);
        for (auto &t : project.transitions)
            if (t.first == pair.first && t.second == pair.second) return t;
        return empty_transition;
    }
    GLGizmoFormorixSurfaces *painter()
    {
        return dynamic_cast<GLGizmoFormorixSurfaces *>(plater.get_view3D_canvas3D()->get_gizmos_manager().get_gizmo(GLGizmosManager::FormorixSurfaces));
    }
    void message(const wxString &text)
    {
        status->Wrap(-1);
        status->SetLabel(text);
        status->Wrap(left->FromDIP(235));
        left->Layout(); left->FitInside();
    }
    void edit(const std::function<void(FS::Project &)> &change)
    {
        if (updating || !writable) return;
        FS::Project candidate = project;
        change(candidate);
        try {
            const std::string encoded = candidate.encode();
            if (encoded == cached_json) return;
            plater.take_snapshot(_u8L("Edit 5D surface settings"));
            project = std::move(candidate);
            store();
        } catch (const std::exception &) {
            message(_L("Invalid value. Check the allowed range and select at least one head or task."));
            refresh_values();
        }
    }
    void edit_settings(const std::function<void(FS::Settings &)> &change)
    {
        if (!region()) return;
        edit([&](FS::Project &p) { change(p.find(active)->settings); });
    }
    void edit_transition(const std::function<void(FS::Transition &)> &change)
    {
        if (!region() || !project.find(neighbor) || active == neighbor) return;
        edit([&](FS::Project &p) {
            const auto pair = std::minmax(active, neighbor);
            auto it = std::find_if(p.transitions.begin(), p.transitions.end(), [&](const FS::Transition &t) {
                return t.first == pair.first && t.second == pair.second;
            });
            if (it == p.transitions.end()) {
                p.transitions.push_back({pair.first, pair.second});
                it = p.transitions.end() - 1;
            }
            change(*it);
        });
    }
    void store()
    {
        cached_json = project.encode();
        plater.model().formorix_surfaces = cached_json;
        plater.set_plater_dirty(true);
        plater.update_title_dirty_status();
        put_other_changes();
        refresh_list();
        refresh_values();
        configure_painter();
    }
    wxStaticText *label(wxWindow *parent, const wxString &text)
    {
        auto *value = new wxStaticText(parent, wxID_ANY, text);
        value->Wrap(parent->FromDIP(parent == left ? 235 : 315));
        parent->GetSizer()->Add(value, 0, wxEXPAND | wxALL, parent->FromDIP(5));
        return value;
    }
    Button *button(wxWindow *parent, const wxString &text, std::function<void()> action)
    {
        auto *value = new Button(parent, text);
        value->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        value->SetToolTip(text);
        value->Bind(wxEVT_BUTTON, [action = std::move(action)](wxCommandEvent &) { action(); });
        parent->GetSizer()->Add(value, 0, wxEXPAND | wxALL, parent->FromDIP(4));
        project_controls.push_back(value);
        region_controls.push_back(value);
        return value;
    }
    wxWindow *group(const wxString &title, bool expanded = false)
    {
        auto *fold = new wxCollapsiblePane(right, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCP_NO_TLW_RESIZE);
        auto *pane = fold->GetPane();
        pane->SetBackgroundColour(right->GetBackgroundColour());
        pane->SetSizer(new wxBoxSizer(wxVERTICAL));
        fold->Collapse(!expanded);
        fold->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent &) { right->Layout(); right->FitInside(); });
        right->GetSizer()->Add(fold, 0, wxEXPAND | wxALL, right->FromDIP(5));
        return pane;
    }
    ::ComboBox *choice(wxWindow *parent, const wxString &title, const std::vector<wxString> &options,
                       std::function<int()> get, std::function<void(int)> set, bool per_region = true)
    {
        label(parent, title);
        auto *value = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
        for (const auto &option : options) value->Append(option);
        value->SetName(title);
        value->Bind(wxEVT_COMBOBOX, [this, value, set](wxCommandEvent &) { if (!updating) set(value->GetSelection()); });
        parent->GetSizer()->Add(value, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(5));
        refreshers.emplace_back([value, get] { value->SetSelection(get()); });
        project_controls.push_back(value);
        if (per_region) region_controls.push_back(value);
        return value;
    }
    void check(wxWindow *parent, const wxString &title, std::function<bool()> get, std::function<void(bool)> set, bool per_region = true)
    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *value = new ::CheckBox(parent);
        value->SetName(title);
        auto *caption = new wxStaticText(parent, wxID_ANY, title);
        caption->Wrap(parent->FromDIP(parent == left ? 200 : 280));
        row->Add(value, 0, wxALIGN_TOP | wxRIGHT, parent->FromDIP(5));
        row->Add(caption, 1, wxEXPAND);
        parent->GetSizer()->Add(row, 0, wxEXPAND | wxALL, parent->FromDIP(5));
        value->Bind(wxEVT_TOGGLEBUTTON, [this, value, set](wxCommandEvent &event) { if (!updating) set(value->GetValue()); event.Skip(); });
        refreshers.emplace_back([value, get] { value->SetValue(get()); });
        project_controls.push_back(value);
        if (per_region) region_controls.push_back(value);
    }
    wxWindow *number(wxWindow *parent, const wxString &title, std::function<double()> get, std::function<void(double)> set,
                double low, double high, bool integer = false, bool per_region = true)
    {
        label(parent, title);
        auto *value = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString,
                                      wxDefaultPosition, parent->FromDIP(wxSize(150, 30)), wxTE_PROCESS_ENTER);
        value->GetTextCtrl()->SetName(title);
        value->SetToolTip(wxString::Format(_L("Allowed range: %1$.4g to %2$.4g"), low, high));
        parent->GetSizer()->Add(value, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(5));
        auto commit = [this, value, set, low, high, integer] {
            if (updating || !value->IsEnabled()) return true;
            double input = 0.;
            if (!value->GetTextCtrl()->GetValue().ToDouble(&input) || !std::isfinite(input) || input < low || input > high ||
                (integer && input != std::floor(input))) {
                message(_L("Invalid number. The previous value is retained."));
                return false;
            }
            set(input);
            return true;
        };
        value->Bind(wxEVT_TEXT_ENTER, [commit](wxCommandEvent &) { commit(); });
        value->Bind(wxEVT_KILL_FOCUS, [commit](wxFocusEvent &event) { commit(); event.Skip(); });
        pending_edits.emplace_back(value->GetTextCtrl(), commit);
        value->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { if (!updating) plater.set_plater_dirty(true); event.Skip(); });
        refreshers.emplace_back([value, get] {
            if (!value->GetTextCtrl()->HasFocus()) value->GetTextCtrl()->ChangeValue(wxString::Format("%.6g", get()));
        });
        if (per_region) region_controls.push_back(value);
        project_controls.push_back(value);
        return value;
    }
    void depend(wxWindow *control, std::function<bool()> enabled) { dependencies.emplace_back(control, std::move(enabled)); }
    void build_left();
    void build_right();
    void build_support(wxWindow *parent);
    void build_heads(wxWindow *parent);
    void refresh_list();
    void refresh_values();
    void configure_painter();
    void select_object(int index);
    void sync();
    void theme()
    {
        for (auto *pane : {left, right}) {
            pane->SetBackgroundColour(wxGetApp().get_window_default_clr());
            wxGetApp().UpdateDarkUIWin(pane);
        }
        if (support_group) support_group->sys_color_changed();
    }
};

void FormorixSurfaceEditor::Impl::build_left()
{
    label(left, _L("5D surface regions"));
    objects = choice(left, _L("Model to paint"), {}, [] { return -1; }, [this](int i) { select_object(i); }, false);
    // This dropdown is synchronized from the model, not a project setting.
    refreshers.pop_back();
    project_controls.pop_back();
    button(left, _L("Add model…"), [this] { plater.add_model(); sync(); });
    project_controls.pop_back(); region_controls.pop_back();
    regions = new wxListBox(left, wxID_ANY, wxDefaultPosition, left->FromDIP(wxSize(235, 150)), 0, nullptr, wxLB_EXTENDED);
    regions->SetName(_L("5D surface regions"));
    left->GetSizer()->Add(regions, 0, wxEXPAND | wxALL, left->FromDIP(5));
    regions->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &event) {
        if (updating) return;
        wxArrayInt rows;
        regions->GetSelections(rows);
        selected.clear();
        for (int row : rows) selected.push_back(project.regions[row].id);
        if (event.GetSelection() >= 0) active = project.regions[event.GetSelection()].id;
        refresh_values(); configure_painter();
    });
    button(left, _L("Add region"), [this] {
        const int id = project.next_id();
        if (!id) { message(_L("This project already has 32 surface regions.")); return; }
        active = id; selected = {id};
        edit([&](FS::Project &p) { p.regions.push_back({id, into_u8(format_wxstr(_L("Region %1%"), id))}); });
        configure_painter();
    });
    region_controls.pop_back(); // A first region can be created without an existing region.
    label(left, _L("Region name"));
    auto *name = new ::TextInput(left, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition,
                                 left->FromDIP(wxSize(220, 30)), wxTE_PROCESS_ENTER);
    name->GetTextCtrl()->SetName(_L("Region name"));
    left->GetSizer()->Add(name, 0, wxEXPAND | wxALL, left->FromDIP(5));
    auto rename = [this, name] {
        if (updating || !region()) return true;
        const std::string value = into_u8(name->GetTextCtrl()->GetValue().Strip(wxString::both));
        if (value.empty()) { message(_L("Enter a region name before saving.")); return false; }
        edit([&](FS::Project &p) { p.find(active)->name = value; });
        return true;
    };
    name->Bind(wxEVT_TEXT_ENTER, [rename](wxCommandEvent &) { rename(); });
    name->Bind(wxEVT_KILL_FOCUS, [rename](wxFocusEvent &e) { rename(); e.Skip(); });
    pending_edits.emplace_back(name->GetTextCtrl(), rename);
    name->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { if (!updating) plater.set_plater_dirty(true); event.Skip(); });
    refreshers.emplace_back([this, name] {
        if (!name->GetTextCtrl()->HasFocus()) name->GetTextCtrl()->ChangeValue(region() ? from_u8(region()->name) : wxString{});
    });
    region_controls.push_back(name);
    button(left, _L("Merge selected regions"), [this] {
        if (!writable || selected.size() < 2 || !region()) { message(_L("Use Ctrl-click to select at least two regions.")); return; }
        plater.take_snapshot(_u8L("Merge 5D surface regions"));
        FS::merge_regions(project, plater.model(), selected, active);
        for (auto *object : plater.model().objects) save_object_mesh(*object);
        selected = {active}; store(); painter()->reload_paint();
        message(_L("Regions merged. The active region's settings and head assignment were retained. Continuous paths are requested, not calculated."));
    });
    button(left, _L("Copy settings to selected regions"), [this] {
        if (!region() || selected.size() < 2) { message(_L("Use Ctrl-click to select the source and destination regions.")); return; }
        const auto source = settings();
        edit([&](FS::Project &p) { for (int id : selected) p.find(id)->settings = source; });
    });
    button(left, _L("Show / hide selected regions"), [this] {
        if (!region()) return;
        const bool visible = !region()->visible;
        edit([&](FS::Project &p) { for (int id : selected) p.find(id)->visible = visible; });
    });
    button(left, _L("Remove selected regions"), [this] {
        if (!writable || selected.empty()) return;
        plater.take_snapshot(_u8L("Remove 5D surface regions"));
        FS::remove_regions(project, plater.model(), selected);
        for (auto *object : plater.model().objects) save_object_mesh(*object);
        active = project.regions.empty() ? 0 : project.regions.front().id;
        selected = active ? std::vector<int>{active} : std::vector<int>{};
        store(); painter()->reload_paint();
    });
    label(left, _L("The last clicked region supplies settings when merging or copying. Ctrl-click selects multiple regions."));
    choice(left, _L("Selection tool"), {_L("Connected surface"), _L("Brush: add"), _L("Brush: erase"), _L("Pick reference edge")},
           [this] { return project.selection.tool; }, [this](int i) { edit([i](auto &p) { p.selection.tool = i; }); });
    number(left, _L("Brush radius (mm)"), [this] { return project.selection.brush_radius; }, [this](double v) { edit([v](auto &p) { p.selection.brush_radius = v; }); }, .4, 8.);
    check(left, _L("Stop connected selection at geometric edges"), [this] { return project.selection.limit_edges; }, [this](bool v) { edit([v](auto &p) { p.selection.limit_edges = v; }); });
    number(left, _L("Edge angle limit (°)"), [this] { return project.selection.edge_angle; }, [this](double v) { edit([v](auto &p) { p.selection.edge_angle = v; }); }, 0., 90.);
    number(left, _L("Minimum surface slope (°)"), [this] { return project.selection.slope_min; }, [this](double v) { edit([v](auto &p) { p.selection.slope_min = v; }); }, 0., 180.);
    number(left, _L("Maximum surface slope (°)"), [this] { return project.selection.slope_max; }, [this](double v) { edit([v](auto &p) { p.selection.slope_max = v; }); }, 0., 180.);
    button(left, _L("Select surfaces in slope range"), [this] {
        if (project.selection.slope_min > project.selection.slope_max) { message(_L("Minimum slope must not exceed maximum slope.")); return; }
        if (auto *g = painter()) g->select_slope(project.selection.slope_min, project.selection.slope_max);
    });
    label(left, _L("Slope is measured between the outward surface normal and global +Z. Left-click paints; Shift-click or right-click erases. Ctrl-drag rotates the view; the wheel zooms."));
    number(left, _L("Section position (0–1)"), [this] { return project.selection.section_position; }, [this](double v) { edit([v](auto &p) { p.selection.section_position = v; }); if (auto *g = painter()) g->section(v); }, 0., 1.);
    button(left, _L("Reset section"), [this] { edit([](auto &p) { p.selection.section_position = 0.; }); if (auto *g = painter()) g->section(0.); refresh_values(); });
    label(left, _L("Section view shows the original model. Shell depth is specified along the local normal; offset geometry is not calculated."));
    status = label(left, _L("Add a region and select a surface on the model. Experimental: no 5D paths or executable 5D output."));
}

void FormorixSurfaceEditor::Impl::build_right()
{
    label(right, _L("Manufacturing intent — not calculated"));
    auto *pane = group(_L("5D layers"), true);
    choice(pane, _L("Shell definition"), {_L("Number of 5D layers"), _L("5D shell thickness"), _L("Entire wall in 5D")},
        [this] { return int(settings().shell_mode); }, [this](int v) { edit_settings([v](auto &s) { s.shell_mode = FS::ShellMode(v); }); });
    depend(number(pane, _L("Number of 5D layers"), [this] { return settings().layers; }, [this](double v) { edit_settings([v](auto &s) { s.layers = int(v); }); }, 1, 10000, true), [this] { return settings().shell_mode == FS::ShellMode::Layers; });
    depend(number(pane, _L("Shell thickness along local normal (mm)"), [this] { return settings().shell_thickness; }, [this](double v) { edit_settings([v](auto &s) { s.shell_thickness = v; }); }, .001, 10000), [this] { return settings().shell_mode == FS::ShellMode::Thickness; });
    number(pane, _L("5D layer thickness (mm)"), [this] { return settings().layer_height; }, [this](double v) { edit_settings([v](auto &s) { s.layer_height = v; }); }, .001, 100);
    check(pane, _L("Preserve original model dimensions"), [this] { return settings().preserve_dimensions; }, [this](bool v) { edit_settings([v](auto &s) { s.preserve_dimensions = v; }); });
    label(pane, _L("The requested shell lies inside the nominal geometry. Depth follows the local surface normal, not global Z. No offset geometry has been generated."));
    wall_warning = label(pane, _L("A wall printed entirely in 5D requires a suitable load-bearing structure before 5D printing. Conventional planar supports may be required first."));
    continuity = label(pane, wxEmptyString);
    check(pane, _L("Restrict region to one print head"), [this] { return settings().single_head; }, [this](bool v) {
        edit_settings([v](auto &s) { s.single_head = v; if (v) s.head_mask &= (~s.head_mask + 1); });
    });
    label(pane, _L("A physically continuous extrusion path requires the same head. Different heads use the transition settings. Connectivity and paths are not yet checked."));

    pane = group(_L("Nozzle orientation"));
    choice(pane, _L("Nozzle orientation"), {_L("Normal to the local surface"), _L("Allow deviation from the normal")},
        [this] { return settings().allow_deviation ? 1 : 0; }, [this](int v) { edit_settings([v](auto &s) { s.allow_deviation = v != 0; }); });
    depend(number(pane, _L("Maximum deviation (°)"), [this] { return settings().max_deviation; }, [this](double v) { edit_settings([v](auto &s) { s.max_deviation = v; }); }, 0, 15), [this] { return settings().allow_deviation; });
    check(pane, _L("Allow vertical surfaces"), [this] { return settings().allow_vertical; }, [this](bool v) { edit_settings([v](auto &s) { s.allow_vertical = v; }); });
    label(pane, _L("Reachability: not checked. A vertical wall normally requires 90° nozzle tilt; the provisional mechanism allows 75°. Up to 15° deviation may help, but only future kinematic checks can confirm this."));

    pane = group(_L("Underlay and supports"));
    check(pane, _L("Use normal infill as underlay"), [this] { return settings().use_infill; }, [this](bool v) { edit_settings([v](auto &s) { s.use_infill = v; }); });
    check(pane, _L("Inherit Infill first"), [this] { return settings().inherit_infill_first; }, [this](bool v) { edit_settings([v](auto &s) { s.inherit_infill_first = v; }); });
    check(pane, _L("Denser underlay / transition structure"), [this] { return settings().dense_underlay; }, [this](bool v) { edit_settings([v](auto &s) { s.dense_underlay = v; }); });
    depend(number(pane, _L("Requested underlay density (%)"), [this] { return settings().underlay_density; }, [this](double v) { edit_settings([v](auto &s) { s.underlay_density = v; }); }, 0, 100), [this] { return settings().dense_underlay; });
    check(pane, _L("Additional supports requested"), [this] { return settings().additional_support; }, [this](bool v) { edit_settings([v](auto &s) { s.additional_support = v; }); });
    choice(pane, _L("Support material strategy"), {_L("Normal breakaway supports"), _L("Water-soluble supports (e.g. PVA)"), _L("Automatic from configured materials")},
        [this] { return int(settings().support_material); }, [this](int v) { edit_settings([v](auto &s) { s.support_material = FS::SupportMaterial(v); }); });
    label(pane, _L("5D support structures are not calculated. The following settings are the existing Orca process settings and also affect normal planar printing."));
    build_support(pane);

    pane = group(_L("Surface properties"));
    choice(pane, _L("Path direction"), {_L("Automatic"), _L("Along selected edge"), _L("Custom direction")},
        [this] { return int(settings().direction); }, [this](int v) { edit_settings([v](auto &s) { s.direction = FS::Direction(v); }); });
    edge_status = label(pane, wxEmptyString);
    button(pane, _L("Select reference edge on model"), [this] { edit([](auto &p) { p.selection.tool = int(GLGizmoFormorixSurfaces::Tool::Edge); }); refresh_values(); message(_L("Click near a mesh edge on the model to store its endpoints.")); });
    for (int axis = 0; axis < 3; ++axis) {
        const std::array<wxString, 3> titles{_L("Direction X"), _L("Direction Y"), _L("Direction Z")};
        depend(number(pane, titles[axis], [this, axis] { return settings().custom_direction[axis]; },
               [this, axis](double v) { edit_settings([axis, v](auto &s) { s.custom_direction[axis] = v; }); }, -1e6, 1e6),
               [this] { return settings().direction == FS::Direction::Custom; });
    }
    check(pane, _L("Alternate directions between layers"), [this] { return settings().alternate_directions; }, [this](bool v) { edit_settings([v](auto &s) { s.alternate_directions = v; }); });
    number(pane, _L("Finer final layers (0 disables)"), [this] { return settings().fine_final_layers; }, [this](double v) { edit_settings([v](auto &s) { s.fine_final_layers = int(v); }); }, 0, 10000, true);
    depend(number(pane, _L("Final layer thickness (mm)"), [this] { return settings().fine_layer_height; }, [this](double v) { edit_settings([v](auto &s) { s.fine_layer_height = v; }); }, .001, 100), [this] { return settings().fine_final_layers > 0; });
    number(pane, _L("Distance from region boundary (mm)"), [this] { return settings().boundary_margin; }, [this](double v) { edit_settings([v](auto &s) { s.boundary_margin = v; }); }, 0, 10000);
    choice(pane, _L("Surface quality"), {_L_CONTEXT("Draft", "Surface quality"), _L("Standard"), _L_CONTEXT("Fine", "Surface quality")}, [this] { return settings().quality; }, [this](int v) { edit_settings([v](auto &s) { s.quality = v; }); });
    check(pane, _L("Request 5D ironing (future function)"), [this] { return settings().ironing; }, [this](bool v) { edit_settings([v](auto &s) { s.ironing = v; }); });
    label(pane, _L("Speed, line width, flow, temperature and cooling are inherited from the assigned Orca material and process profiles. Surface paths and ironing are not calculated."));

    pane = group(_L("Transitions between regions"));
    transition_target = choice(pane, _L("Other region"), {}, [this] {
        const auto it = std::find(neighbor_ids.begin(), neighbor_ids.end(), neighbor);
        return it == neighbor_ids.end() ? -1 : int(it - neighbor_ids.begin());
    }, [this](int i) { if (i >= 0 && i < int(neighbor_ids.size())) neighbor = neighbor_ids[i]; refresh_values(); });
    depend(choice(pane, _L("Transition type"), {_L("Fixed boundary"), _L("Interwoven transition"), _L("Parallel paths")},
        [this] { return int(transition().kind); }, [this](int v) { edit_transition([v](auto &t) { t.kind = FS::TransitionKind(v); }); }), [this] { return neighbor != 0; });
    depend(number(pane, _L("Interwoven zone width (mm)"), [this] { return transition().width; }, [this](double v) { edit_transition([v](auto &t) { t.width = v; }); }, .001, 10000), [this] { return neighbor != 0 && transition().kind == FS::TransitionKind::Interwoven; });
    depend(number(pane, _L("Desired overlap (%)"), [this] { return transition().overlap; }, [this](double v) { edit_transition([v](auto &t) { t.overlap = v; }); }, 0, 100), [this] { return neighbor != 0 && transition().kind == FS::TransitionKind::Interwoven; });
    depend(choice(pane, _L("Interwoven pattern"), {_L("Automatic"), _L("Toothed"), _L("Alternating between layers")},
        [this] { return int(transition().pattern); }, [this](int v) { edit_transition([v](auto &t) { t.pattern = FS::TransitionPattern(v); }); }), [this] { return neighbor != 0 && transition().kind == FS::TransitionKind::Interwoven; });
    label(pane, _L("Fixed: each head stays in its assigned region. Interwoven: the regions interlock in the requested zone. Parallel: two heads travel close together over the complete region. These are intentions only; no paths, spacing or collision safety have been calculated."));
    label(pane, _L("Adjacency and material compatibility: not checked. Some materials do not bond reliably. A later technical check is required before any manufacturing strategy can be released."));

    pane = group(_L("Head assignment"));
    for (int i = 0; i < 3; ++i)
        check(pane, format_wxstr(_L("Assign to head %1%"), i + 1), [this, i] { return (settings().head_mask & (1u << i)) != 0; }, [this, i](bool v) {
            edit_settings([i, v](auto &s) { if (v && s.single_head) s.head_mask = 1u << i; else if (v) s.head_mask |= 1u << i; else s.head_mask &= ~(1u << i); });
        });
    label(pane, _L("Head assignments describe future 5D work. They do not change Orca's normal extruder or support-material assignments."));
    build_heads(group(_L("Multi-head operation")));
}

void FormorixSurfaceEditor::Impl::build_support(wxWindow *parent)
{
    support_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    support_group = std::make_unique<ConfigOptionsGroup>(parent, _L("Orca support settings"), &support_config);
    support_group->label_width = 17;
    for (const char *key : {"enable_support", "support_type", "support_style", "support_threshold_angle", "support_on_build_plate_only",
                           "support_base_pattern", "support_base_pattern_spacing", "support_top_z_distance", "support_bottom_z_distance",
                           "support_object_xy_distance", "support_interface_top_layers", "support_interface_bottom_layers",
                           "support_interface_pattern", "support_interface_spacing", "support_filament", "support_interface_filament", "is_infill_first"}) {
        if (support_config.has(key)) support_group->append_single_option_line(key);
    }
    support_group->m_on_change = [this](const std::string &key, const boost::any &) {
        if (updating) return;
        if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
            try { tab->apply_preset_config_value(key, support_config.opt_serialize(key)); }
            catch (const std::exception &) { message(_L("Orca could not apply this support setting. The current process values have been restored.")); }
            support_signature.clear(); sync();
        }
    };
    support_group->activate();
    parent->GetSizer()->Add(support_group->sizer, 0, wxEXPAND);
    support_group->reload_config();
}

void FormorixSurfaceEditor::Impl::build_heads(wxWindow *parent)
{
    choice(parent, _L("Operating strategy"), {_L("Automatic parallel operation"), _L("Material-dependent task allocation"), _L("High-speed mode")},
        [this] { return int(project.strategy); }, [this](int v) {
            if (v == int(FS::Strategy::HighSpeed) && !high_speed_ready) {
                message(_L("High-speed mode requires three enabled heads with compatible PETG profiles. Check the multi-head group."));
                refresh_values(); return;
            }
            edit([v](auto &p) { p.strategy = FS::Strategy(v); });
        }, false);
    label(parent, _L("Automatic parallel operation requests simultaneous work when possible; unavailable heads wait. Allocation, timing and collisions are not calculated."));
    head_status = label(parent, wxEmptyString);
    const std::array<wxString, 5> tasks{_L("Normal part construction"), _L("5D surfaces"), _L("Support structures"), _L("Reinforcement regions (e.g. CF)"), _L("Automatic task allocation")};
    for (int i = 0; i < 3; ++i) {
        label(parent, format_wxstr(_L("Head %1%"), i + 1));
        check(parent, _L("Enabled"), [this, i] { return project.heads[i].enabled; }, [this, i](bool v) { edit([i, v](auto &p) { p.heads[i].enabled = v; }); }, false);
        materials[i] = choice(parent, _L("Orca material profile"), {}, [this, i] {
            const auto it = std::find(material_names.begin(), material_names.end(), project.heads[i].material_preset);
            return it == material_names.end() ? 0 : int(it - material_names.begin());
        }, [this, i](int value) {
            if (value >= 0 && value < int(material_names.size())) edit([&](auto &p) { p.heads[i].material_preset = material_names[value]; });
        }, false);
        for (int role = 0; role < 5; ++role)
            check(parent, tasks[role], [this, i, role] { return (project.heads[i].tasks & (1u << role)) != 0; }, [this, i, role](bool v) {
                edit([i, role, v](auto &p) {
                    auto &roles = p.heads[i].tasks;
                    if (role == 4 && v) roles = FS::Automatic;
                    else if (v) roles = (roles & ~unsigned(FS::Automatic)) | (1u << role);
                    else roles &= ~(1u << role);
                });
            }, false);
        assignments[i] = label(parent, wxEmptyString);
    }
    label(parent, _L("Example: head 1 PETG for the part, head 2 PVA for supports, head 3 PETG-CF for reinforcement. Material suitability and bonding remain unverified."));
}

void FormorixSurfaceEditor::Impl::refresh_list()
{
    updating = true;
    regions->Clear();
    for (const auto &r : project.regions) {
        const wxString text = format_wxstr(r.visible ? _L("%1% · %2%") : _L("%1% · %2% (hidden)"), r.id, from_u8(r.name));
        const int row = regions->Append(text);
        if (std::find(selected.begin(), selected.end(), r.id) != selected.end()) regions->SetSelection(row);
    }
    updating = false;
}
void FormorixSurfaceEditor::Impl::refresh_values()
{
    updating = true;
    neighbor_ids.clear(); transition_target->Clear();
    for (const auto &r : project.regions) if (r.id != active) { neighbor_ids.push_back(r.id); transition_target->Append(from_u8(r.name)); }
    if (std::find(neighbor_ids.begin(), neighbor_ids.end(), neighbor) == neighbor_ids.end()) neighbor = neighbor_ids.empty() ? 0 : neighbor_ids.front();
    for (const auto &refresh : refreshers) refresh();
    // Avoid transient disable/enable cycles on every edit; native custom controls
    // must retain focus and repaint consistently inside the scrolling panes.
    for (auto *control : project_controls) {
        const bool per_region = std::find(region_controls.begin(), region_controls.end(), control) != region_controls.end();
        bool enabled = writable && (!per_region || region() != nullptr);
        for (const auto &entry : dependencies) if (entry.first == control) enabled &= entry.second();
        if (control->IsEnabled() != enabled) control->Enable(enabled);
    }
    for (auto *control : region_controls)
        if (std::find(project_controls.begin(), project_controls.end(), control) == project_controls.end())
            control->Enable(writable && region() != nullptr);
    // wxStaticText caches the last wrap width even after SetLabel changes its text.
    // Reset it first so updated status messages remain fully readable.
    continuity->Wrap(-1);
    continuity->SetLabel(region() && region()->continuous ? _L("Cross-surface paths: continuous") : _L("Merge regions to request continuous cross-surface paths."));
    continuity->Wrap(right->FromDIP(315));
    wall_warning->Show(region() && settings().shell_mode == FS::ShellMode::FullWall);
    edge_status->Wrap(-1);
    edge_status->SetLabel(settings().edge_selected ? _L("Reference edge stored. Path direction not calculated.") : _L("No reference edge selected."));
    edge_status->Wrap(right->FromDIP(315));
    bool petg = true;
    bool same_profile = true;
    int temperature_min = 0, temperature_max = 1000;
    std::string first_profile;
    for (int i = 0; i < 3; ++i) {
        wxString names;
        for (const auto &r : project.regions) if ((r.settings.head_mask & (1u << i)) != 0) {
            if (!names.empty()) names += ", "; names += from_u8(r.name);
        }
        assignments[i]->Wrap(-1);
        assignments[i]->SetLabel(format_wxstr(_L("Assigned regions: %1%"), names.empty() ? _L("None") : names));
        assignments[i]->Wrap(right->FromDIP(315));
        const auto &h = project.heads[i];
        const Preset *preset = wxGetApp().preset_bundle->filaments.find_preset(h.material_preset);
        const auto *types = preset ? preset->config.option<ConfigOptionStrings>("filament_type") : nullptr;
        petg &= h.enabled && preset && preset->is_compatible && types && !types->values.empty() && types->values.front() == "PETG";
        const auto *low = preset ? preset->config.option<ConfigOptionInts>("nozzle_temperature_range_low") : nullptr;
        const auto *high = preset ? preset->config.option<ConfigOptionInts>("nozzle_temperature_range_high") : nullptr;
        if (low && !low->values.empty()) temperature_min = std::max(temperature_min, low->values.front());
        else petg = false;
        if (high && !high->values.empty()) temperature_max = std::min(temperature_max, high->values.front());
        else petg = false;
        if (i == 0) first_profile = h.material_preset; else same_profile &= h.material_preset == first_profile;
    }
    high_speed_ready = petg && temperature_min <= temperature_max;
    head_status->Wrap(-1);
    head_status->SetLabel(high_speed_ready ? (same_profile ?
        _L("High-speed prerequisite: three enabled heads with the same PETG profile. Technical approval, collisions and scheduling: not checked.") :
        _L("Three compatible PETG profiles with overlapping temperature ranges selected. Calibration, collisions and scheduling are not checked; high-speed remains a requested strategy without technical approval.")) :
        _L("High-speed prerequisite not met: assign compatible PETG profiles to all three enabled heads. The selected strategy is only a request."));
    head_status->Wrap(right->FromDIP(315));
    right->Layout(); right->FitInside();
    left->Refresh(); right->Refresh();
    updating = false;
}
void FormorixSurfaceEditor::Impl::configure_painter()
{
    if (!shown) return;
    auto &manager = plater.get_view3D_canvas3D()->get_gizmos_manager();
    auto *g = painter();
    if (!g) return;
    g->configure(project, writable && region() ? active : 0, GLGizmoFormorixSurfaces::Tool(project.selection.tool), float(project.selection.brush_radius), float(project.selection.limit_edges ? project.selection.edge_angle : 180.));
    g->edge_selected = [this](const std::array<double, 6> &edge) {
        // Defer native control updates until the canvas mouse handler has returned.
        left->CallAfter([this, edge] {
            if (!shown || wxGetApp().is_closing()) return;
            edit_settings([&](auto &s) { s.edge = edge; s.edge_selected = true; s.direction = FS::Direction::Edge; });
            message(_L("Reference edge stored for this region. No path direction has been calculated."));
        });
    };
    if (region() && writable && manager.get_current_type() != GLGizmosManager::FormorixSurfaces && g->is_activable())
        manager.open_gizmo(GLGizmosManager::FormorixSurfaces);
}
void FormorixSurfaceEditor::Impl::select_object(int index)
{
    if (index < 0 || index >= int(plater.model().objects.size())) return;
    auto *canvas = plater.get_view3D_canvas3D();
    canvas->get_gizmos_manager().reset_all_states();
    canvas->get_selection().add_instance(index, 0, true);
    canvas->get_gizmos_manager().update_data();
    configure_painter();
    canvas->set_as_dirty();
}
void FormorixSurfaceEditor::Impl::sync()
{
    if (wxGetApp().is_closing()) return;
    const auto &model = plater.model();
    if (cached_json != model.formorix_surfaces) {
        cached_json = model.formorix_surfaces;
        std::string error;
        auto data = FS::Project::decode(cached_json, error);
        writable = bool(data);
        project = data ? std::move(*data) : FS::Project{};
        if (!region()) { active = project.regions.empty() ? 0 : project.regions.front().id; selected = active ? std::vector<int>{active} : std::vector<int>{}; }
        refresh_list(); refresh_values();
        if (!writable) message(_L("This project's 5D data is unsupported or invalid. It is preserved unchanged; editing is disabled."));
        if (auto *g = painter()) g->reload_paint();
        configure_painter();
    }
    std::string names;
    for (const auto *object : model.objects) names += std::to_string(object->id().id) + object->name + '\n';
    if (names != object_signature) {
        object_signature = names;
        objects->Clear();
        for (const auto *object : model.objects) objects->Append(from_u8(object->name));
        if (!model.objects.empty()) { objects->SetSelection(0); select_object(0); }
    }
    std::vector<std::string> available{std::string{}};
    for (const Preset &preset : wxGetApp().preset_bundle->filaments.get_presets())
        if (!preset.is_default) available.push_back(preset.name);
    for (const auto &h : project.heads)
        if (!h.material_preset.empty() && std::find(available.begin(), available.end(), h.material_preset) == available.end()) available.push_back(h.material_preset);
    if (available != material_names) {
        material_names = std::move(available);
        for (auto *box : materials) {
            box->Clear(); box->Append(_L("Not assigned"));
            for (size_t i = 1; i < material_names.size(); ++i) box->Append(from_u8(material_names[i]));
        }
        refresh_values();
    }
    const auto &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    std::string signature;
    for (const auto &entry : support_group->opt_map()) signature += config.opt_serialize(entry.second.first) + '\n';
    if (signature != support_signature) {
        support_signature = signature; updating = true;
        support_config = config; support_group->reload_config(); updating = false;
    }
}

FormorixSurfaceEditor::FormorixSurfaceEditor(Plater &plater) : m_impl(std::make_unique<Impl>(plater)) {}
FormorixSurfaceEditor::~FormorixSurfaceEditor() = default;
wxWindow *FormorixSurfaceEditor::left() const { return m_impl->left; }
wxWindow *FormorixSurfaceEditor::right() const { return m_impl->right; }
void FormorixSurfaceEditor::show(bool visible)
{
    auto &p = *m_impl;
    p.shown = visible;
    if (visible) {
        p.sync(); p.refresh_list(); p.refresh_values(); p.configure_painter(); p.timer.Start(500);
        if (auto *g = p.painter()) g->section(p.project.selection.section_position);
    } else {
        p.timer.Stop();
        auto &manager = p.plater.get_view3D_canvas3D()->get_gizmos_manager();
        if (manager.get_current_type() == GLGizmosManager::FormorixSurfaces) manager.reset_all_states();
        if (auto *g = p.painter()) g->edge_selected = {};
    }
}
bool FormorixSurfaceEditor::commit_pending_edit()
{
    if (!m_impl->shown) return true;
    wxWindow *focused = wxWindow::FindFocus();
    for (const auto &edit : m_impl->pending_edits)
        if (edit.first == focused) return edit.second();
    return true;
}
void FormorixSurfaceEditor::sys_color_changed() { m_impl->theme(); }
void FormorixSurfaceEditor::rescale()
{
    if (m_impl->support_group) m_impl->support_group->msw_rescale();
    std::vector<wxWindow *> controls = m_impl->project_controls;
    controls.insert(controls.end(), m_impl->region_controls.begin(), m_impl->region_controls.end());
    controls.push_back(m_impl->objects);
    std::set<wxWindow *> unique(controls.begin(), controls.end());
    for (auto *control : unique) {
        if (auto *button = dynamic_cast<Button *>(control)) button->Rescale();
        else if (auto *check = dynamic_cast<::CheckBox *>(control)) check->Rescale();
        else if (auto *choice = dynamic_cast<::ComboBox *>(control)) choice->Rescale();
        else if (auto *input = dynamic_cast<::TextInput *>(control)) input->Rescale();
    }
    for (auto *pane : {m_impl->left, m_impl->right}) { pane->Layout(); pane->FitInside(); }
}
}
