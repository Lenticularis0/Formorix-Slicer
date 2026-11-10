#pragma once
#include <memory>
class wxWindow;

namespace Slic3r::GUI {
class Plater;
// Lazily constructed side panes of the existing Plater, sharing its model, canvas and undo history.
class FormorixSurfaceEditor {
public:
    explicit FormorixSurfaceEditor(Plater &plater);
    ~FormorixSurfaceEditor();
    wxWindow *left() const;
    wxWindow *right() const;
    wxWindow *bottom() const;
    bool has_preview();
    bool preview_visible() const;
    void show_preview(bool visible);
    void show(bool visible);
    bool commit_pending_edit();
    void sys_color_changed();
    void rescale();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
