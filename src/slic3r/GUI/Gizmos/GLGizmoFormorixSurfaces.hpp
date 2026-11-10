#pragma once

#include "GLGizmoPainterBase.hpp"
#include "GLGizmoBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include <wx/event.h>
#include <wx/string.h>
#include "libslic3r/FormorixSurfaces.hpp"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::GUI {
class GLGizmoFormorixSurfaces : public GLGizmoPainterBase {
public:
    enum class Tool { Connected, Brush, Erase, Edge };
    GLGizmoFormorixSurfaces(GLCanvas3D &parent, unsigned int sprite_id);
    void configure(const FormorixSurfaces::Project &project, int region, Tool tool, float radius, float edge_angle);
    void reload_paint();
    void select_slope(double minimum, double maximum);
    void section(double position);
    std::function<void(const std::array<double, 6> &)> edge_selected;
    void render_painter_gizmo() override;
    bool on_mouse(const wxMouseEvent &event) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse, bool shift, bool alt, bool control) override;

protected:
    bool on_init() override { return true; }
    // This painter belongs to the 5D tab, not the standard Prepare toolbar.
    bool on_is_selectable() const override { return false; }
    void on_render_input_window(float, float, float) override {}
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    void on_opening() override {}
    void on_shutdown() override;
    // Use the ordinary project undo history so settings and paint remain one transaction stream.
    bool wants_enter_leave_snapshots() const override { return false; }
    wxString handle_snapshot_action_name(bool, Button) const override;
    EnforcerBlockerType get_left_button_state_type() const override;
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType::NONE; }

private:
    int m_region = 0;
    Tool m_tool = Tool::Connected;
    std::vector<ColorRGBA> m_colors;
    bool pick_edge(const Vec2d &mouse);
};
}
