#pragma once

#include <functional>
#include <string>
#include <vector>
#include <glib-object.h>

/* What a dock button carries while it is dragged: its launcher name, or the
 * desktop id of a running app. The type has no serializer, so no other app can
 * accept the drop and the drag never leaves the dock process. */
struct WfDockDragData
{
    gchar *name;
    gboolean pinned;
};

GType wf_dock_drag_data_get_type();

/* The launchers option, edited in place in wf-shell.ini */
namespace launcher_config
{
std::vector<std::string> get();
/* Puts name in front of before, or last if before is empty or not pinned.
 * A launcher that is already pinned moves. */
void pin(const std::string& name, const std::string& before);
void unpin(const std::string& name);

/* Changing the launchers rebuilds the dock's buttons, including the one being
 * dragged, and GTK still signals that button when the drag ends. So the drop
 * only stores the change, and the end of the drag applies it. */
void defer(std::function<void()> change);
void run_deferred();
}
