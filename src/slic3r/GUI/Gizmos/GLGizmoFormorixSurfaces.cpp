#include "GLGizmoFormorixSurfaces.hpp"
#include "GLGizmoPainterBase.hpp"
#include "GLGizmoBase.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include <array>
#include <cstddef>
#include <utility>

#include "GLGizmosCommon.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include <glad/gl.h>
#include <wx/event.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace Slic3r::GUI {
GLGizmoFormorixSurfaces::GLGizmoFormorixSurfaces(GLCanvas3D &parent, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, "mmu_segmentation.svg", sprite_id),
      m_colors(FormorixSurfaces::max_regions + 1, ColorRGBA(.7f, .7f, .7f, 1.f))
{
    m_tool_type = ToolType::SMART_FILL;
    m_cursor_type = TriangleSelector::CIRCLE;
}
std::string GLGizmoFormorixSurfaces::on_get_name() const { return _u8L("5D Surfaces"); }
PainterGizmoType GLGizmoFormorixSurfaces::get_painter_type() const { return PainterGizmoType::FORMORIX_SURFACES; }
wxString GLGizmoFormorixSurfaces::handle_snapshot_action_name(bool, Button) const { return _L("Edit 5D surface selection"); }
EnforcerBlockerType GLGizmoFormorixSurfaces::get_left_button_state_type() const
{
    return EnforcerBlockerType(m_tool == Tool::Erase ? 0 : m_region);
}
void GLGizmoFormorixSurfaces::configure(const FormorixSurfaces::Project &p, int region, Tool tool, float radius, float angle)
{
    m_region = region;
    m_tool = tool;
    m_tool_type = tool == Tool::Connected ? ToolType::SMART_FILL : ToolType::BRUSH;
    m_cursor_radius = radius;
    m_smart_fill_angle = angle;
    static const std::array<ColorRGBA, 8> palette{{
        {.10f, .72f, .84f, 1.f}, {1.f, .56f, .17f, 1.f}, {.66f, .39f, .92f, 1.f},
        {.25f, .81f, .43f, 1.f}, {.94f, .33f, .49f, 1.f}, {.93f, .79f, .21f, 1.f},
        {.28f, .48f, .93f, 1.f}, {.83f, .52f, .69f, 1.f}}};
    std::fill(m_colors.begin(), m_colors.end(), ColorRGBA(.7f, .7f, .7f, 1.f));
    for (const auto &r : p.regions)
        if (r.visible) m_colors[r.id] = palette[(r.id - 1) % palette.size()];
    for (auto &selector : m_triangle_selectors)
        static_cast<TriangleSelectorPatch *>(selector.get())->set_ebt_colors(m_colors);
    m_parent.set_as_dirty();
}
void GLGizmoFormorixSurfaces::reload_paint()
{
    if (get_state() == On && m_c->selection_info() && m_c->selection_info()->model_object())
        update_from_model_object(false);
    m_parent.set_as_dirty();
}
void GLGizmoFormorixSurfaces::update_from_model_object(bool)
{
    m_triangle_selectors.clear();
    const ModelObject *object = m_c->selection_info()->model_object();
    if (!object) return;
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        auto selector = std::make_unique<TriangleSelectorPatch>(volume->mesh(), m_colors);
        if (FormorixSurfaces::painting_matches(*volume))
            selector->deserialize(volume->formorix_facets.get_data(), false);
        selector->request_update_render_data();
        m_triangle_selectors.emplace_back(std::move(selector));
    }
}
void GLGizmoFormorixSurfaces::update_model_object()
{
    ModelObject *object = m_c->selection_info()->model_object();
    if (!object) return;
    size_t index = 0;
    bool changed = false;
    for (ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        if (index >= m_triangle_selectors.size()) return;
        changed |= volume->formorix_facets.set(*m_triangle_selectors[index++]);
        volume->formorix_topology = FormorixSurfaces::topology_id(volume->mesh().its);
    }
    if (changed) {
        wxGetApp().plater()->set_plater_dirty(true);
        wxGetApp().plater()->update_title_dirty_status();
        save_object_mesh(*object);
    }
}
void GLGizmoFormorixSurfaces::on_shutdown()
{
    m_parent.toggle_model_objects_visibility(true);
}
void GLGizmoFormorixSurfaces::render_painter_gizmo()
{
    if(preview&&preview->result){m_preview_renderer.render(*preview,wxGetApp().plater()->get_camera());return;}
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_triangles(m_parent.get_selection());
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    if (m_region != 0 && m_tool != Tool::Edge) render_cursor();
    glsafe(::glDisable(GL_BLEND));
}
bool GLGizmoFormorixSurfaces::on_mouse(const wxMouseEvent &event)
{
    if(preview)return false;
    if (m_region == 0) return false;
    if (m_tool == Tool::Edge) {
        if (event.LeftDown() && !event.CmdDown()) return pick_edge(Vec2d(event.GetX(), event.GetY()));
        return false;
    }
    return GLGizmoPainterBase::on_mouse(event);
}
bool GLGizmoFormorixSurfaces::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse, bool shift, bool alt, bool control)
{
    if(preview)return false;
    if (m_region == 0 || m_tool == Tool::Edge) return false;
    return GLGizmoPainterBase::gizmo_event(action, mouse, shift, alt, control);
}
void GLGizmoFormorixSurfaces::section(double position)
{
    if (get_state() != On || !m_c->object_clipper()) return;
    m_c->object_clipper()->set_behavior(true, true, 1.);
    if (position > 0.) m_parent.align_section_view_to_camera();
    m_parent.set_section_view_ratio(position);
    m_parent.set_as_dirty();
}
void GLGizmoFormorixSurfaces::select_slope(double minimum, double maximum)
{
    if (get_state() != On || m_region == 0) return;
    ModelObject *object = m_c->selection_info()->model_object();
    const int instance = m_parent.get_selection().get_instance_idx();
    if (!object || instance < 0) return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Select 5D surfaces by slope"));
    size_t index = 0;
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        const auto &mesh = volume->mesh().its;
        const Matrix3d normal_matrix = (object->instances[instance]->get_matrix() * volume->get_matrix()).linear().inverse().transpose();
        auto &selector = m_triangle_selectors[index++];
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            const auto &t = mesh.indices[i];
            Vec3d n = normal_matrix * (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]).cast<double>();
            if (n.squaredNorm() < 1e-20) continue;
            const double angle = std::acos(std::clamp(n.normalized().z(), -1., 1.)) * 180. / std::acos(-1.);
            if (angle >= minimum && angle <= maximum) selector->set_facet(static_cast<int>(i), EnforcerBlockerType(m_region));
        }
        // Rebuild the colored patches as well as their GPU buffers after bulk selection.
        selector->request_update_render_data(true);
    }
    update_model_object();
    m_parent.set_as_dirty();
}
bool GLGizmoFormorixSurfaces::pick_edge(const Vec2d &mouse)
{
    const ModelObject *object = m_c->selection_info()->model_object();
    const int instance = m_parent.get_selection().get_instance_idx();
    if (!object || instance < 0) return false;
    const Camera &camera = wxGetApp().plater()->get_camera();
    double closest = std::numeric_limits<double>::max();
    std::array<double, 6> edge{};
    size_t index = 0;
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part()) continue;
        const Transform3d transform = object->instances[instance]->get_matrix() * volume->get_matrix();
        Vec3f hit, normal;
        size_t facet = 0;
        const auto *raycaster = m_c->raycaster()->raycasters()[index++];
        if (!raycaster->unproject_on_mesh(mouse, transform, camera, hit, normal, m_c->object_clipper()->get_clipping_plane(), &facet)) continue;
        const double distance = (transform * hit.cast<double>() - camera.get_position()).squaredNorm();
        if (distance >= closest) continue;
        closest = distance;
        const auto &mesh = volume->mesh().its;
        const auto &triangle = mesh.indices[facet];
        double nearest_edge = std::numeric_limits<double>::max();
        for (int i = 0; i < 3; ++i) {
            const Vec3d a = mesh.vertices[triangle[i]].cast<double>();
            const Vec3d b = mesh.vertices[triangle[(i + 1) % 3]].cast<double>();
            const Vec3d delta = b - a;
            const double t = delta.squaredNorm() == 0. ? 0. : std::clamp((hit.cast<double>() - a).dot(delta) / delta.squaredNorm(), 0., 1.);
            const double d = (hit.cast<double>() - a - t * delta).squaredNorm();
            if (d < nearest_edge) { nearest_edge = d; edge = {a.x(), a.y(), a.z(), b.x(), b.y(), b.z()}; }
        }
    }
    if (closest == std::numeric_limits<double>::max()) return false;
    if (edge_selected) edge_selected(edge);
    return true;
}
}
