#include "FormorixSurfaceEditor.hpp"
#include "FormorixHelp.hpp"
#include "FormorixPreviewView.hpp"
#include "MainFrame.hpp"
#include "Jobs/Worker.hpp"
#include "Jobs/Job.hpp"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixUnderlay.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/FormorixProfiles.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include <cstddef>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <exception>
#include <memory>
#include <optional>

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
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace Slic3r::GUI {
namespace FS = FormorixSurfaces;
namespace FP = FormorixPreview;
namespace FH = FormorixHybrid;
namespace FC = FormorixCorePreview;
namespace FR = FormorixProfiles;

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
    std::array<::ComboBox *, 3> nozzle_presets{};
    std::array<::ComboBox *, 3> process_presets{};
    std::vector<std::string> material_names;
    std::vector<std::string> printer_names;
    std::vector<std::string> process_names;
    std::array<std::string, 3> source_signatures;
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
    FormorixPreviewPanel *preview=nullptr;
    bool preview_mode=false;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    std::string preview_signature;
    FormorixHelpTopic help_topic=FormorixHelpTopic::Surfaces;

    explicit Impl(Plater &owner) : plater(owner), timer(this)
    {
        left = new wxScrolledWindow(&plater, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxCLIP_CHILDREN);
        right = new wxScrolledWindow(&plater, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxCLIP_CHILDREN);
        for (auto *pane : {left, right}) {
            pane->SetBackgroundColour(wxGetApp().get_window_default_clr());
            pane->SetSizer(new wxBoxSizer(wxVERTICAL));
            pane->SetScrollRate(0, pane->FromDIP(12));
        }
        preview=new FormorixPreviewPanel(&plater,[this]{plater.get_view3D_canvas3D()->set_as_dirty();},[this]{
            preview->set_result({});preview_signature.clear();
            wxGetApp().mainframe->select_tab(TAB_ID_PREPARE);
            wxGetApp().mainframe->request_select_tab(TAB_ID_PREVIEW);
        });
        preview->Hide();
        build_left();
        build_right();
        Bind(wxEVT_TIMER, [this](wxTimerEvent &) { if (shown) sync(); });
        theme();
    }
    ~Impl() override { *alive=false;timer.Stop(); }

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
        try {
            change(candidate);
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
        auto *row=new wxBoxSizer(wxHORIZONTAL);
        auto *value = new wxStaticText(parent, wxID_ANY, text);
        value->Wrap(parent->FromDIP(parent == left ? 200 : 280));
        row->Add(value,1,wxALIGN_CENTER_VERTICAL);
        if(!text.empty())row->Add(formorix_help_button(parent,help_topic),0,wxLEFT,parent->FromDIP(4));
        parent->GetSizer()->Add(row, 0, wxEXPAND | wxALL, parent->FromDIP(5));
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
    wxWindow *group(const wxString &title, bool expanded = false,FormorixHelpTopic topic=FormorixHelpTopic::Surfaces)
    {
        help_topic=topic;
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
        row->Add(formorix_help_button(parent,help_topic),0,wxLEFT,parent->FromDIP(4));
        parent->GetSizer()->Add(row, 0, wxEXPAND | wxALL, parent->FromDIP(5));
        value->Bind(wxEVT_TOGGLEBUTTON, [this, value, set](wxCommandEvent &event) { if (!updating) set(value->GetValue()); event.Skip(); });
        refreshers.emplace_back([value, get] { value->SetValue(get()); });
        project_controls.push_back(value);
        if (per_region) region_controls.push_back(value);
    }
    wxWindow *number(wxWindow *parent, const wxString &title, std::function<double()> get, std::function<void(double)> set,
                double low, double high, bool integer = false, bool per_region = true)
    {
        wxString caption=title,unit;
        for(const auto&suffix:{wxString(" (mm)"),wxString::FromUTF8(" (°)"),wxString(" (%)")})
            if(caption.EndsWith(suffix)){unit=suffix.Mid(2,suffix.size()-3);caption.RemoveLast(suffix.size());break;}
        label(parent, caption);
        auto *value = new ::TextInput(parent, wxEmptyString, unit, wxEmptyString,
                                      wxDefaultPosition, parent->FromDIP(wxSize(150, 30)), wxTE_PROCESS_ENTER);
        value->GetTextCtrl()->SetName(title);
        value->SetToolTip(wxString::Format(_L("Allowed range: %1$.4g to %2$.4g"), low, high));
        parent->GetSizer()->Add(value, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(5));
        auto commit = [this, value, get, set, low, high, integer] {
            if (updating || !value->IsEnabled()) return true;
            // Focusing an unchanged rounded display must not replace its exact
            // stored value, especially an automatic width used as a fixed limit.
            if (value->GetTextCtrl()->GetValue() == wxString::Format("%.6g", get())) return true;
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
    void calculate();
    void calculate_hybrid();
    std::string fingerprint() const;
    bool validate_preview();
    void theme()
    {
        for (auto *pane : {left, right}) {
            pane->SetBackgroundColour(wxGetApp().get_window_default_clr());
            wxGetApp().UpdateDarkUIWin(pane);
        }
        if (support_group) support_group->sys_color_changed();
        if(preview)preview->theme();
    }
};

void FormorixSurfaceEditor::Impl::build_left()
{
    help_topic=FormorixHelpTopic::Selection;
    label(left, _L("5D surface regions"));
    objects = choice(left, _L("Model to paint"), {}, [] { return -1; }, [this](int i) { select_object(i); }, false);
    // This dropdown is synchronized from the model, not a project setting.
    refreshers.pop_back();
    project_controls.pop_back();
    button(left, _L("Add model…"), [this] { plater.add_model(); sync(); });
    project_controls.pop_back(); region_controls.pop_back();
    button(left,_L("Calculate 5D preview"),[this]{calculate();});
    button(left,_L("Calculate hybrid preview"),[this]{calculate_hybrid();});
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
    button(left, _L("Merge regions"), [this] {
        if (!writable || selected.size() < 2 || !region()) { message(_L("Use Ctrl-click to select at least two regions.")); return; }
        plater.take_snapshot(_u8L("Merge 5D surface regions"));
        FS::merge_regions(project, plater.model(), selected, active);
        for (auto *object : plater.model().objects) save_object_mesh(*object);
        selected = {active}; store(); painter()->reload_paint();
        message(_L("Regions merged. Calculate the preview to check continuity."));
    });
    button(left, _L("Copy settings"), [this] {
        if (!region() || selected.size() < 2) { message(_L("Use Ctrl-click to select the source and destination regions.")); return; }
        const auto source = settings();
        edit([&](FS::Project &p) { for (int id : selected) p.find(id)->settings = source; });
    });
    button(left, _L("Show / hide regions"), [this] {
        if (!region()) return;
        const bool visible = !region()->visible;
        edit([&](FS::Project &p) { for (int id : selected) p.find(id)->visible = visible; });
    });
    button(left, _L("Remove regions"), [this] {
        if (!writable || selected.empty()) return;
        plater.take_snapshot(_u8L("Remove 5D surface regions"));
        FS::remove_regions(project, plater.model(), selected);
        for (auto *object : plater.model().objects) save_object_mesh(*object);
        active = project.regions.empty() ? 0 : project.regions.front().id;
        selected = active ? std::vector<int>{active} : std::vector<int>{};
        store(); painter()->reload_paint();
    });
    label(left, _L("Ctrl-click: select multiple regions."));
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
    label(left, _L("Ctrl-drag: rotate. Wheel: zoom."));
    number(left, _L("Section position (0–1)"), [this] { return project.selection.section_position; }, [this](double v) { edit([v](auto &p) { p.selection.section_position = v; }); if (auto *g = painter()) g->section(v); }, 0., 1.);
    button(left, _L("Reset section"), [this] { edit([](auto &p) { p.selection.section_position = 0.; }); if (auto *g = painter()) g->section(0.); refresh_values(); });
    label(left, _L("Section: original model."));
    status = label(left, _L("Select surfaces, then calculate the preview."));
}

void FormorixSurfaceEditor::Impl::build_right()
{
    label(right, _L("Experimental 5D preview"));
    auto *pane = group(_L("5D layers"), true,FormorixHelpTopic::ShellDepth);
    choice(pane, _L("Shell definition"), {_L("Number of 5D layers"), _L("5D shell thickness"), _L("Entire wall in 5D")},
        [this] { return int(settings().shell_mode); }, [this](int v) { edit_settings([v](auto &s) { s.shell_mode = FS::ShellMode(v); }); });
    depend(number(pane, _L("Number of 5D layers"), [this] { return settings().layers; }, [this](double v) { edit_settings([v](auto &s) { s.layers = int(v); }); }, 1, 10000, true), [this] { return settings().shell_mode == FS::ShellMode::Layers; });
    depend(number(pane, _L("Shell thickness along local normal (mm)"), [this] { return settings().shell_thickness; }, [this](double v) { edit_settings([v](auto &s) { s.shell_thickness = v; }); }, .001, 10000), [this] { return settings().shell_mode == FS::ShellMode::Thickness; });
    help_topic=FormorixHelpTopic::SurfaceLayerHeight;
    number(pane, _L("5D layer thickness (mm)"), [this] { return settings().layer_height; }, [this](double v) { edit_settings([v](auto &s) { s.layer_height = v; }); }, .001, 100);
    check(pane, _L("Preserve original model dimensions"), [this] { return settings().preserve_dimensions; }, [this](bool v) { edit_settings([v](auto &s) { s.preserve_dimensions = v; }); });
    label(pane, _L("Offsets follow the local normal."));
    help_topic=FormorixHelpTopic::Supports;
    wall_warning = label(pane, _L("Full-wall 5D requires a supporting structure."));
    help_topic=FormorixHelpTopic::ContinuousPaths;
    continuity = label(pane, wxEmptyString);
    check(pane, _L("Restrict region to one print head"), [this] { return settings().single_head; }, [this](bool v) {
        edit_settings([v](auto &s) { s.single_head = v; if (v) s.head_mask &= (~s.head_mask + 1); });
    });
    label(pane, _L("Preview: one head per region."));

    pane = group(_L("Nozzle orientation"), false,FormorixHelpTopic::NozzleDeviation);
    choice(pane, _L("Nozzle orientation"), {_L("Normal to the local surface"), _L("Allow deviation from the normal")},
        [this] { return settings().allow_deviation ? 1 : 0; }, [this](int v) { edit_settings([v](auto &s) { s.allow_deviation = v != 0; }); });
    depend(number(pane, _L("Maximum deviation (°)"), [this] { return settings().max_deviation; }, [this](double v) { edit_settings([v](auto &s) { s.max_deviation = v; }); }, 0, 15), [this] { return settings().allow_deviation; });
    check(pane, _L("Allow vertical surfaces"), [this] { return settings().allow_vertical; }, [this](bool v) { edit_settings([v](auto &s) { s.allow_vertical = v; }); });
    label(pane, _L("Machine reachability: not checked."));

    pane = group(_L("Underlay and supports"), false,FormorixHelpTopic::Supports);
    check(pane, _L("Use normal infill as underlay"), [this] { return settings().use_infill; }, [this](bool v) { edit_settings([v](auto &s) { s.use_infill = v; }); });
    check(pane, _L("Inherit Infill first"), [this] { return settings().inherit_infill_first; }, [this](bool v) { edit_settings([v](auto &s) { s.inherit_infill_first = v; }); });
    check(pane, _L("Denser underlay / transition structure"), [this] { return settings().dense_underlay; }, [this](bool v) { edit_settings([v](auto &s) { s.dense_underlay = v; }); });
    depend(number(pane, _L("Requested underlay density (%)"), [this] { return settings().underlay_density; }, [this](double v) { edit_settings([v](auto &s) { s.underlay_density = v; }); }, 0, 100), [this] { return settings().dense_underlay; });
    check(pane, _L("Additional supports requested"), [this] { return settings().additional_support; }, [this](bool v) { edit_settings([v](auto &s) { s.additional_support = v; }); });
    choice(pane, _L("Support material strategy"), {_L("Normal breakaway supports"), _L("Water-soluble supports (e.g. PVA)"), _L("Automatic from configured materials")},
        [this] { return int(settings().support_material); }, [this](int v) { edit_settings([v](auto &s) { s.support_material = FS::SupportMaterial(v); }); });
    label(pane, _L("Shared Orca settings. 5D supports: not calculated."));
    build_support(pane);

    pane = group(_L("Surface properties"), false,FormorixHelpTopic::ContinuousPaths);
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
    label(pane, _L("Orca profiles inherited. 5D ironing: not calculated."));

    pane = group(_L("Transitions between regions"), false,FormorixHelpTopic::Transitions);
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
    label(pane, _L("Multi-head transitions: not calculated."));
    label(pane, _L("Material compatibility: not checked."));

    pane = group(_L("Head assignment"), false,FormorixHelpTopic::MultiHead);
    for (int i = 0; i < 3; ++i)
        check(pane, format_wxstr(_L("Assign to head %1%"), i + 1), [this, i] { return (settings().head_mask & (1u << i)) != 0; }, [this, i](bool v) {
            edit_settings([i, v](auto &s) { if (v && s.single_head) s.head_mask = 1u << i; else if (v) s.head_mask |= 1u << i; else s.head_mask &= ~(1u << i); });
        });
    label(pane, _L("One assigned head is planned in the preview."));
    build_heads(group(_L("Multi-head operation"), false,FormorixHelpTopic::MultiHead));
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
    label(parent, _L("Multi-head scheduling: not calculated."));
    help_topic=FormorixHelpTopic::HighSpeed;
    head_status = label(parent, wxEmptyString);
    help_topic=FormorixHelpTopic::MultiHead;
    const std::array<wxString, 5> tasks{_L("Normal part construction"), _L("5D surfaces"), _L("Support structures"), _L("Reinforcement regions (e.g. CF)"), _L("Automatic task allocation")};
    for (int i = 0; i < 3; ++i) {
        label(parent, format_wxstr(_L("Head %1%"), i + 1));
        check(parent, _L("Enabled"), [this, i] { return project.heads[i].enabled; }, [this, i](bool v) { edit([i, v](auto &p) { p.heads[i].enabled = v; }); }, false);
        materials[i] = choice(parent, _L("Orca material profile"), {}, [this, i] {
            const auto it = std::find(material_names.begin(), material_names.end(), project.heads[i].material_preset);
            return it == material_names.end() ? 0 : int(it - material_names.begin());
        }, [this, i](int value) {
            if (value >= 0 && value < int(material_names.size())) edit([&](auto &p) { p.heads[i].assign_material(material_names[value]); });
        }, false);
        nozzle_presets[i] = choice(parent, _L("Nozzle preset (Orca printer)"), {}, [this, i] {
            const auto it = std::find(printer_names.begin(), printer_names.end(), project.heads[i].printer_preset);
            return it == printer_names.end() ? 0 : int(it - printer_names.begin());
        }, [this, i](int value) {
            if (value >= 0 && value < int(printer_names.size()))
                edit([&](auto& p) { p.heads[i].assign_nozzle(printer_names[value], 0); });
        }, false);
        number(parent, _L("Nozzle slot"), [this, i] { return project.heads[i].nozzle_index + 1.; },
            [this, i](double value) { edit([&](auto& p) { p.heads[i].assign_nozzle(p.heads[i].printer_preset, unsigned(value) - 1); }); },
            1., 32., true, false);
        process_presets[i] = choice(parent, _L("Orca process profile"), {}, [this, i] {
            const auto it = std::find(process_names.begin(), process_names.end(), project.heads[i].process_preset);
            return it == process_names.end() ? 0 : int(it - process_names.begin());
        }, [this, i](int value) {
            if (value >= 0 && value < int(process_names.size()))
                edit([&](auto& p) { p.heads[i].assign_process(process_names[value]); });
        }, false);
        check(parent, _L("Declare local line width limits"), [this, i] { return project.heads[i].local_width_limits.has_value(); },
            [this, i](bool enabled) { edit([&](auto& p) {
                if (!enabled) p.heads[i].assign_width_limits(std::nullopt);
                else {
                    const double width = FR::resolve(FR::source(*wxGetApp().preset_bundle, p.heads[i])).process.line_width;
                    p.heads[i].assign_width_limits(FormorixProcess::WidthLimits{width, width});
                }
            }); }, false);
        for (bool minimum : {true, false}) {
            const wxString title = minimum ? _L("Minimum local line width (mm)") : _L("Maximum local line width (mm)");
            auto* control = number(parent, title, [this, i, minimum] {
                const auto& limits = project.heads[i].local_width_limits;
                return limits ? (minimum ? limits->minimum_mm : limits->maximum_mm) : 0.;
            }, [this, i, minimum](double value) { edit([&](auto& p) {
                if (!p.heads[i].local_width_limits) return;
                auto limits = *p.heads[i].local_width_limits;
                (minimum ? limits.minimum_mm : limits.maximum_mm) = value;
                p.heads[i].assign_width_limits(limits);
            }); }, .01, 10., false, false);
            depend(control, [this, i] { return project.heads[i].local_width_limits.has_value(); });
        }
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
    label(parent, _L("Material suitability: not checked."));
    label(parent, _L("Declared width limits are not material calibration."));
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
    wall_warning->GetContainingSizer()->ShowItems(region() && settings().shell_mode == FS::ShellMode::FullWall);
    edge_status->Wrap(-1);
    edge_status->SetLabel(settings().edge_selected ? _L("Reference edge stored.") : _L("No reference edge selected."));
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
        _L("Same PETG profile. Technical approval: pending.") :
        _L("Compatible PETG profiles. Technical approval: pending.")) :
        _L("High-speed prerequisite: not met."));
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
    g->preview=preview_mode?preview->state():nullptr;
    g->configure(project, writable && region() ? active : 0, GLGizmoFormorixSurfaces::Tool(project.selection.tool), float(project.selection.brush_radius), float(project.selection.limit_edges ? project.selection.edge_angle : 180.));
    g->edge_selected = [this](const std::array<double, 6> &edge) {
        // Defer native control updates until the canvas mouse handler has returned.
        left->CallAfter([this, edge] {
            if (!shown || wxGetApp().is_closing()) return;
            edit_settings([&](auto &s) { s.edge = edge; s.edge_selected = true; s.direction = FS::Direction::Edge; });
            message(_L("Reference edge stored."));
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
    validate_preview();
    const auto &model = plater.model();
    if (cached_json != model.formorix_surfaces) {
        cached_json = model.formorix_surfaces;
        std::string error;
        auto data = FS::Project::decode(cached_json, error);
        writable = bool(data);
        project = data ? std::move(*data) : FS::Project{};
        source_signatures = {};
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
            box->Clear(); box->Append(_L("Current Orca material"));
            for (size_t i = 1; i < material_names.size(); ++i) box->Append(from_u8(material_names[i]));
        }
        refresh_values();
    }
    std::vector<std::string> printers{std::string{}};
    for (const auto& preset : wxGetApp().preset_bundle->printers.get_presets())
        if (!preset.is_default) printers.push_back(preset.name);
    for (const auto& head : project.heads)
        if (!head.printer_preset.empty() && std::find(printers.begin(), printers.end(), head.printer_preset) == printers.end())
            printers.push_back(head.printer_preset);
    if (printers != printer_names) {
        printer_names = std::move(printers);
        for (auto* box : nozzle_presets) {
            box->Clear(); box->Append(_L("Current Orca printer"));
            for (std::size_t i = 1; i < printer_names.size(); ++i) box->Append(from_u8(printer_names[i]));
        }
        refresh_values();
    }
    std::vector<std::string> processes{std::string{}};
    for (const auto& preset : wxGetApp().preset_bundle->prints.get_presets())
        if (!preset.is_default) processes.push_back(preset.name);
    for (const auto& head : project.heads)
        if (!head.process_preset.empty() && std::find(processes.begin(), processes.end(), head.process_preset) == processes.end())
            processes.push_back(head.process_preset);
    if (processes != process_names) {
        process_names = std::move(processes);
        for (auto* box : process_presets) {
            box->Clear(); box->Append(_L("Current Orca process"));
            for (std::size_t i = 1; i < process_names.size(); ++i) box->Append(from_u8(process_names[i]));
        }
        refresh_values();
    }
    if (writable && FR::invalidate_changed(project, *wxGetApp().preset_bundle, source_signatures)) {
        store();
        message(_L("Orca profile changed. Derived head values were invalidated and will be refreshed before calculation."));
    }
    const auto &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    std::string signature;
    for (const auto &entry : support_group->opt_map()) signature += config.opt_serialize(entry.second.first) + '\n';
    if (signature != support_signature) {
        support_signature = signature; updating = true;
        support_config = config; support_group->reload_config(); updating = false;
    }
}

std::string FormorixSurfaceEditor::Impl::fingerprint() const
{
    std::ostringstream out;out<<std::setprecision(17)<<plater.model().formorix_surfaces;
    const auto&selection=plater.get_view3D_canvas3D()->get_selection();
    const int object=selection.get_object_idx(),instance=selection.get_instance_idx();
    out<<'|'<<object<<'|'<<instance<<'|'<<active;
    if(object<0||object>=int(plater.model().objects.size()))return out.str();
    const auto*model=plater.model().objects[object];out<<'|'<<model->id().id;
    if(instance<0||instance>=int(model->instances.size()))return out.str();
    out<<model->instances[instance]->get_matrix().matrix();
    auto config=[&](const DynamicPrintConfig&c){for(const auto&key:c.keys())out<<key<<'='<<c.opt_serialize(key)<<';';};
    config(model->config.get());
    for (const auto& range : model->layer_config_ranges) {
        out << range.first.first << ':' << range.first.second;
        config(range.second.get());
    }
    for (double height : model->layer_height_profile.get()) out << ':' << height;
    std::uint64_t hash=14695981039346656037ull;
    auto bytes=[&](const void*data,std::size_t count){auto*p=static_cast<const unsigned char*>(data);while(count--){hash^=*p++;hash*=1099511628211ull;}};
    for(const auto*v:model->volumes){
        out<<v->id().id<<':'<<int(v->type())<<':'<<v->formorix_facets.timestamp()<<':'<<v->get_matrix().matrix();
        config(v->config.get());
        const auto&mesh=v->mesh().its;for(const auto&p:mesh.vertices)bytes(p.data(),sizeof(float)*3);for(const auto&t:mesh.indices)bytes(t.data(),sizeof(int)*3);
    }
    out<<hash;
    const auto*bundle=wxGetApp().preset_bundle;
    config(bundle->prints.get_edited_preset().config);config(bundle->printers.get_edited_preset().config);config(bundle->filaments.get_edited_preset().config);
    for (std::size_t head = 0; head < project.heads.size(); ++head) {
        out << "|formorix_source[" << head << "]=";
        try {
            out << FR::signature(FR::source(*bundle, project.heads[head]));
        } catch (const std::exception& error) {
            // Missing/deleted sources must invalidate a running result as well;
            // no fallback to the current preset or a serialized snapshot.
            out << "unresolved:" << error.what();
        }
    }
    config(bundle->project_config);
    return out.str();
}
bool FormorixSurfaceEditor::Impl::validate_preview()
{
    if(!preview->state()->result)return false;
    if(preview_signature!=fingerprint()){
        preview->set_result({});preview_signature.clear();
        message(_L("Model or settings changed. Calculate the preview again."));
        return false;
    }
    return true;
}
void FormorixSurfaceEditor::Impl::calculate()
{
    wxWindow*focused=wxWindow::FindFocus();
    for(const auto&edit:pending_edits)if(edit.first==focused&&!edit.second())return;
    if(!region()||!writable)return;
    if(!plater.get_ui_job_worker().is_idle()){message(_L("Wait for the current operation to finish."));return;}
    preview->pause();
    try {
        FP::Options options;options.region=active;
        unsigned mask=settings().head_mask;
        if(!mask||(mask&(mask-1)))throw FP::Issue::MultipleHeads;
        options.head=mask==1?0:mask==2?1:2;
        if(!project.heads[options.head].enabled)throw FP::Issue::MultipleHeads;
        const auto&selection=plater.get_view3D_canvas3D()->get_selection();
        int object=selection.get_object_idx(),instance=selection.get_instance_idx();
        if(object<0||object>=int(plater.model().objects.size())||instance<0)throw FP::Issue::Empty;
        auto*model=plater.model().objects[object];
        std::size_t faces=0;for(const auto*v:model->volumes)faces+=v->mesh().its.indices.size();
        if(faces>500000)throw FP::Issue::TooLarge;
        auto settings_copy=settings();
        if(settings_copy.direction==FS::Direction::Edge){const auto d=FP::reference_direction(*model,instance,settings_copy);settings_copy.custom_direction={d.x(),d.y(),d.z()};settings_copy.direction=FS::Direction::Custom;}
        auto input=FP::extract(*model,instance,active);
        const auto*bundle=wxGetApp().preset_bundle;
        const auto resolved=FR::resolve(FR::source(*bundle,project.heads[options.head],settings_copy.layer_height));
        const auto&config=resolved.config;
        options.line_width=resolved.process.line_width;
        options.speed=FormorixProcess::effective_speed(resolved.process);
        options.travel_speed=config.opt_float("travel_speed",resolved.nozzle_index);
        if(const auto*tilt=config.option<ConfigOptionFloat>("formorix_5d_max_tilt"))options.max_tilt=tilt->value;
        const std::string signature=fingerprint();auto result=std::make_shared<FP::Result>();auto life=alive;
        message(_L("Calculating 5D preview…"));
        const bool queued=queue_job(plater.get_ui_job_worker(),[input=std::move(input),settings_copy,options,result](Job::Ctl&ctl){
            ctl.update_status(0,_u8L("Calculating 5D preview…"));
            *result=FP::generate(input,settings_copy,options,[&ctl]{return ctl.was_canceled();});
            ctl.update_status(100);
        },[this,life,result,signature](bool canceled,std::exception_ptr&error){
            const bool failed=bool(error);error=nullptr;
            if(!*life||wxGetApp().is_closing())return;
            if(canceled){message(formorix_preview_error(FP::Issue::Canceled));return;}
            if(failed){message(formorix_preview_error(FP::Issue::InvalidMesh));return;}
            if(signature!=fingerprint()){message(_L("Model or settings changed. Calculate the preview again."));return;}
            preview_signature=signature;preview->set_result(result);
            message(result->ok()?_L("Preview ready. Experimental export remains blocked."):formorix_preview_error(result->errors.front()));
            wxGetApp().mainframe->request_select_tab(TAB_ID_PREVIEW);
        });
        if(!queued)message(_L("Preview could not be queued. Try again."));
    }catch(FP::Issue issue){message(formorix_preview_error(issue));}
    catch(const std::exception& e){message(format_wxstr(_L("5D preview failed: %1%"),from_u8(e.what())));}
}

void FormorixSurfaceEditor::Impl::calculate_hybrid()
{
    wxWindow* focused = wxWindow::FindFocus();
    for (const auto& input : pending_edits) if (input.first == focused && !input.second()) return;
    if (!region() || !writable) return;
    if (!plater.get_ui_job_worker().is_idle()) { message(_L("Wait for the current operation to finish.")); return; }
    preview->pause();
    try {
        sync();
        const auto& selection = plater.get_view3D_canvas3D()->get_selection();
        const int object = selection.get_object_idx(), instance = selection.get_instance_idx();
        if (object < 0 || object >= int(plater.model().objects.size()) || instance < 0) throw FP::Issue::Empty;
        const auto* model = plater.model().objects[object];
        std::size_t face_count = 0;
        for (const auto* volume : model->volumes) face_count += volume->mesh().its.indices.size();
        if (face_count > 500000) throw FP::Issue::TooLarge;
        const auto& bundle = *wxGetApp().preset_bundle;
        std::array<FC::Source, 3> sources;
        std::array<FormorixProcess::Profile, 3> profiles;
        for (int head = 0; head < 3; ++head) if (project.heads[head].enabled) {
            auto resolved = FR::resolve(FR::source(bundle, project.heads[head]));
            profiles[head] = resolved.process;
            sources[head] = {std::move(resolved.config), resolved.nozzle_index, resolved.material_index, resolved.process};
            source_signatures[head] = std::move(resolved.source_signature);
        }
        edit([&](auto& candidate) {
            for (int head = 0; head < 3; ++head)
                if (candidate.heads[head].enabled) candidate.heads[head].process = profiles[head];
                else candidate.heads[head].process.reset();
        });
        auto assigned = [&](unsigned role) {
            for (int head = 0; head < 3; ++head)
                if (project.heads[head].enabled && (project.heads[head].tasks & role)) return head;
            for (int head = 0; head < 3; ++head)
                if (project.heads[head].enabled && (project.heads[head].tasks & FS::Automatic)) return head;
            return -1;
        };
        const int core_head = assigned(FS::Build);
        if (core_head < 0) { message(_L("Assign an enabled head to normal construction or automatic tasks.")); return; }
        const int support_head = assigned(FS::Support);
        auto input = FH::extract(*model, instance, project, profiles);
        const auto& machine = bundle.printers.get_edited_preset().config;
        for (auto& region : input.regions)
            if (const auto* tilt = machine.option<ConfigOptionFloat>("formorix_5d_max_tilt")) region.max_tilt = tilt->value;
        const std::string signature = fingerprint();
        auto hybrid = std::make_shared<FH::Result>();
        auto conventional = std::make_shared<FC::Result>();
        auto underlay_preparation = std::make_shared<FormorixUnderlay::Preparation>();
        auto underlay_before = std::make_shared<FormorixUnderlay::Analysis>();
        auto underlay_after = std::make_shared<FormorixUnderlay::Analysis>();
        auto life = alive;
        message(_L("Calculating hybrid geometry and Orca core paths…"));
        const bool queued = queue_job(plater.get_ui_job_worker(),
            [input = std::move(input), sources = std::move(sources), core_head, support_head, hybrid, conventional,
             underlay_preparation, underlay_before, underlay_after](Job::Ctl& ctl) {
                ScopeGuard finish_status([&ctl] { ctl.update_status(100); });
                auto canceled = [&ctl] { return ctl.was_canceled(); };
                ctl.update_status(0, _u8L("Calculating hybrid geometry…"));
                *hybrid = FH::generate(input, canceled);
                if (!hybrid->ok() || canceled()) return;
                double conversion_bound = 0.;
                auto account_conversion = [&] {
                    const double bound = conventional->error.empty() ?
                        std::max(conventional->float_conversion_bound, conventional->modifier_float_conversion_bound) +
                            conventional->coordinate_quantization_bound + conventional->native_slice_expansion_bound : INFINITY;
                    for (auto& region : hybrid->regions)
                        region.accuracy.bounds[2] += std::max(0., bound - conversion_bound);
                    conversion_bound = std::max(conversion_bound, bound);
                };
                ctl.update_status(35, _u8L("Calculating Orca core paths…"));
                if (!hybrid->core.faces.empty()) {
                    *conventional = FC::slice(hybrid->core, sources, core_head, support_head, canceled);
                    account_conversion();
                    if (!conventional->ok() || canceled()) return;
                }
                ctl.update_status(85, _u8L("Checking geometric underlay…"));
                *underlay_preparation = FormorixUnderlay::prepare(*hybrid, {}, canceled);
                if (!underlay_preparation->ok() || canceled()) return;
                auto check_underlay = [&] {
                    std::vector<FH::UnderlaySegment> underlay;
                    for (const auto& path : conventional->paths) {
                        if (path.nominal_supplied_mm3_per_mm <= 0.) continue;
                        for (std::size_t i = 1; i < path.points.size(); ++i)
                            underlay.push_back({path.points[i - 1], path.points[i], path.width, path.height,
                                path.head, path.process.material_id, path.support, path.role == erInternalInfill,
                                is_bridge(path.role), path.flow_geometry_unverified, path.dense_transition});
                    }
                    FH::audit_underlay(*hybrid, underlay, canceled);
                    return FormorixUnderlay::analyze(*hybrid, *underlay_preparation, *conventional, {}, canceled);
                };
                *underlay_before = check_underlay();
                if (!underlay_before->ok() || canceled()) return;
                *underlay_after = *underlay_before;
                if (!underlay_before->dense_requests.empty()) {
                    ctl.update_status(90, _u8L("Calculating local Orca underlay…"));
                    auto revised = FC::slice(hybrid->core, sources, core_head, support_head, canceled,
                                             underlay_before->dense_requests);
                    if (!revised.ok() || canceled()) {
                        // Keep the measured baseline and its visible gaps. A failed
                        // density request does not erase the previously valid paths.
                        *underlay_after = {};
                        underlay_after->error = revised.error;
                        return;
                    }
                    *conventional = std::move(revised);
                    account_conversion();
                    *underlay_after = check_underlay();
                    if (!underlay_after->ok() || canceled()) return;
                }
            },
            [this, life, hybrid, conventional, signature, underlay_preparation, underlay_before, underlay_after]
            (bool canceled, std::exception_ptr& error) {
                wxString failure;
                if (error) {
                    try { std::rethrow_exception(error); }
                    catch (const std::exception& e) { failure = from_u8(e.what()); }
                    catch (...) { failure = _L("Hybrid preview calculation failed."); }
                    error = nullptr;
                }
                if (!*life || wxGetApp().is_closing()) return;
                if (canceled) { message(formorix_preview_error(FP::Issue::Canceled)); return; }
                if (!failure.empty()) { message(format_wxstr(_L("Hybrid preview failed: %1%"), failure)); return; }
                if (signature != fingerprint()) { message(_L("Model or settings changed. Calculate the preview again.")); return; }
                preview_signature = signature;
                preview->set_hybrid(hybrid, conventional, underlay_preparation, underlay_before, underlay_after);
                if (!hybrid->error.empty()) message(format_wxstr(_L("Hybrid geometry failed: %1%"), from_u8(hybrid->error)));
                else if (!conventional->error.empty()) message(format_wxstr(_L("Orca core preview failed: %1%"), from_u8(conventional->error)));
                else if (!underlay_preparation->ok() || !underlay_before->ok() || !underlay_after->ok())
                    message(_L("Underlay verification is incomplete. See the geometry report."));
                else message(_L("Hybrid preview ready. Review coverage, material volume and underlay; export remains blocked."));
                plater.get_view3D_canvas3D()->zoom_to_selection();
                wxGetApp().mainframe->request_select_tab(TAB_ID_PREVIEW);
            });
        if (!queued) message(_L("Preview could not be queued. Try again."));
    } catch (FP::Issue issue) { message(formorix_preview_error(issue)); }
    catch (const std::exception& e) { message(format_wxstr(_L("Hybrid preview failed: %1%"), from_u8(e.what()))); }
}

FormorixSurfaceEditor::FormorixSurfaceEditor(Plater &plater) : m_impl(std::make_unique<Impl>(plater)) {}
FormorixSurfaceEditor::~FormorixSurfaceEditor() = default;
wxWindow *FormorixSurfaceEditor::left() const { return m_impl->left; }
wxWindow *FormorixSurfaceEditor::right() const { return m_impl->right; }
wxWindow *FormorixSurfaceEditor::bottom() const {return m_impl->preview;}
bool FormorixSurfaceEditor::has_preview(){return m_impl->validate_preview();}
bool FormorixSurfaceEditor::preview_visible()const{return m_impl->preview_mode;}
void FormorixSurfaceEditor::show_preview(bool visible)
{
    auto&p=*m_impl;
    if(p.preview_mode==visible)return;
    p.preview_mode=visible;
    if(!visible)p.preview->pause();
    auto*canvas=p.plater.get_view3D_canvas3D();
    canvas->enable_main_toolbar(!visible);
    canvas->enable_picking(!visible);
    canvas->enable_moving(!visible);
    canvas->get_gizmos_manager().set_toolbar_enabled(!visible);
    p.configure_painter();
    if(auto*g=p.painter())g->section(visible?0.:p.project.selection.section_position);
}
void FormorixSurfaceEditor::show(bool visible)
{
    auto &p = *m_impl;
    p.shown = visible;
    if (visible) {
        p.sync(); p.refresh_list(); p.refresh_values(); p.configure_painter(); p.timer.Start(500);
        if (auto *g = p.painter()) g->section(p.project.selection.section_position);
    } else {
        p.preview->pause();
        p.timer.Stop();
        auto &manager = p.plater.get_view3D_canvas3D()->get_gizmos_manager();
        if (manager.get_current_type() == GLGizmosManager::FormorixSurfaces) manager.reset_all_states();
        if (auto *g = p.painter()) {g->edge_selected = {};g->preview.reset();}
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
    m_impl->preview->rescale();
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
