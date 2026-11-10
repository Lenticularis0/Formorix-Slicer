#include "FormorixPreviewView.hpp"
#include "FormorixHelp.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "Camera.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"
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
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>
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

namespace Slic3r::GUI {
namespace FP = FormorixPreview;
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
    wxStaticText *clock, *heads;
    ::ComboBox *layers, *speed;
    std::vector<Button*> buttons;
    wxTimer timer;
    std::chrono::steady_clock::time_point last;
    double rate = 1.;
    Impl(FormorixPreviewPanel& owner, std::function<void()> on_change, std::function<void()> ordinary)
        : panel(owner), changed(std::move(on_change)), timer(this)
    {
        panel.SetBackgroundColour(wxGetApp().get_window_default_clr());
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
        auto* options = new wxBoxSizer(wxHORIZONTAL);
        layers        = new ::ComboBox(&panel, wxID_ANY, wxEmptyString, wxDefaultPosition, panel.FromDIP(wxSize(170, 30)), 0, nullptr,
                                       wxCB_READONLY);
        layers->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
            state->layer = layers->GetSelection() - 1;
            changed();
        });
        options->Add(layers, 0, wxALL, panel.FromDIP(3));
        auto flag = [&](const wxString& name, bool FormorixPreviewState::* field) {
            auto* c = new ::CheckBox(&panel);
            c->SetName(name);
            c->Bind(wxEVT_TOGGLEBUTTON, [this, c, field](wxCommandEvent& e) {
                state.get()->*field = c->GetValue();
                changed();
                e.Skip();
            });
            options->Add(c, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, panel.FromDIP(8));
            options->Add(new wxStaticText(&panel, wxID_ANY, name), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, panel.FromDIP(4));
        };
        flag(_L("5D shell"), &FormorixPreviewState::shell);
        flag(_L("Remaining core"), &FormorixPreviewState::core);
        auto* normal = new Button(&panel, _L("Orca preview"));
        normal->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        normal->Bind(wxEVT_BUTTON, [ordinary](wxCommandEvent&) { ordinary(); });
        options->Add(normal, 0, wxALL, panel.FromDIP(3));
        buttons.push_back(normal);
        layout->Add(options, 0, wxEXPAND);
        heads = new wxStaticText(&panel, wxID_ANY, wxEmptyString);
        layout->Add(heads, 0, wxALL, panel.FromDIP(5));
        auto* scope = new wxStaticText(&panel, wxID_ANY,
            _L("5D part only: excludes normal construction, supports and waiting times."));
        layout->Add(scope, 0, wxLEFT | wxRIGHT, panel.FromDIP(5));
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
        panel.Layout();
    }
};
FormorixPreviewPanel::FormorixPreviewPanel(wxWindow* p, std::function<void()> changed, std::function<void()> ordinary)
    : wxPanel(p), m_impl(std::make_unique<Impl>(*this, std::move(changed), std::move(ordinary)))
{}
FormorixPreviewPanel::~FormorixPreviewPanel() = default;
std::shared_ptr<FormorixPreviewState> FormorixPreviewPanel::state() const { return m_impl->state; }
void FormorixPreviewPanel::pause() { m_impl->timer.Stop(); }
void FormorixPreviewPanel::set_result(std::shared_ptr<const FP::Result> result)
{
    pause();
    auto& p          = *m_impl;
    p.state->result  = std::move(result);
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
            for (std::size_t j = 0; j < segments.size(); ++j) {
                const auto& m = *segments[j];
                unsigned base = unsigned(path.vertices_count());
                path.add_vertex(Vec3f(m.from.position.cast<float>()));
                path.add_vertex(Vec3f(m.to.position.cast<float>()));
                path.add_line(base, base + 1);
                display->ends.push_back(m.end);
                if (j % std::max(std::size_t(1), segments.size() / 12) == 0) {
                    Vec3d d = m.to.position - m.from.position;
                    if (d.norm() < 1e-8)
                        continue;
                    d.normalize();
                    Vec3d side = d.cross(m.to.axis);
                    if (side.norm() < 1e-8)
                        continue;
                    side.normalize();
                    base          = unsigned(arrows.vertices_count());
                    const auto& p = m.to.position;
                    arrows.add_vertex(Vec3f((p + .55 * d).cast<float>()));
                    arrows.add_vertex(Vec3f((p - .35 * d + .25 * side).cast<float>()));
                    arrows.add_vertex(Vec3f((p - .35 * d - .25 * side).cast<float>()));
                    arrows.add_triangle(base, base + 1, base + 2);
                }
            }
            display->path.init_from(std::move(path));
            if (!arrows.is_empty())
                display->arrows.init_from(std::move(arrows));
            display->start  = segments.front()->from.position;
            display->finish = segments.back()->to.position;
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
void FormorixPreviewRenderer::render(const FormorixPreviewState& s, const Camera& camera)
{
    if (!s.result)
        return;
    if (m_result != s.result) {
        m_result = s.result;
        rebuild(*m_result);
    }
    auto* shader = wxGetApp().get_shader("flat");
    if (!shader)
        return;
    shader->start_using();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (s.core) {
        m_core.set_color(ColorRGBA(.55f, .57f, .6f, .35f));
        m_core.render();
    }
    if (s.shell) {
        glDepthMask(GL_FALSE);
        for (std::size_t l = 0; l < m_shells.size(); ++l) {
            if (s.layer >= 0 && int(l) != s.layer)
                continue;
            auto& mesh = *m_shells[l];
            mesh.set_color(ColorRGBA(.2f, .75f, .85f, .12f));
            mesh.render();
        }
        glDepthMask(GL_TRUE);
    }
    m_invalid.set_color(ColorRGBA(1.f, .15f, .1f, .7f));
    m_invalid.render();
    // Lines are a preview overlay, so inner layers remain visible through the context mesh.
    glDisable(GL_DEPTH_TEST);
    for (auto& display : m_paths) {
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
        if (s.layer < 0 || display->layer == s.layer) {
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
            if (state.segment < track.size() && state.kind != FP::MotionKind::Wait) {
                m_active.reset();
                m_active.init_from(lines({track[state.segment].from, track[state.segment].to}));
                m_active.set_color(ColorRGBA(1.f, 1.f, 1.f, 1.f));
                m_active.render();
            }
        }
    for (const auto& display : m_paths) {
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
