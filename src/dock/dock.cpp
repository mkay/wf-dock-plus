#include <gtkmm/window.h>
#include <gdkmm/frameclock.h>
#include <glibmm/main.h>
#include <gdk/wayland/gdkwayland.h>

#include <gtk-utils.hpp>
#include <wf-shell-app.hpp>
#include <gtk4-layer-shell.h>
#include <wf-autohide-window.hpp>

#include "dock.hpp"
#include "app-group.hpp"
#include "launchers.hpp"
#include "places.hpp"
#include "pin.hpp"
#include "trash.hpp"
#include "../util/gtk-utils.hpp"
#include "gtkmm/flowbox.h"
#include "gtkmm/separator.h"
#include <gtkmm/droptarget.h>
#include <css-config.hpp>


class WfDock::impl
{
    WayfireOutput *output;
    std::unique_ptr<WayfireAutohidingWindow> window;
    wl_surface *_wl_surface;
    Gtk::Box layout;
    WfDockPlaces places;
    WfDockLaunchers launchers;
    Gtk::Separator separator;
    Gtk::FlowBox box;
    Gtk::Separator trash_separator;
    WfDockTrash trash;
    /* Running apps go into their launcher, or into box if they have none */
    WfDockApps apps{launchers,
        [this] (Gtk::Widget& widget) { add_child(widget); },
        [this] (Gtk::Widget& widget) { rem_child(widget); }};

    WfOption<int> entries_per_line{DOCK_SECTION "/max_per_line"};
    WfOption<std::string> position{DOCK_SECTION "/position"};

    Glib::RefPtr<Gtk::DropTarget> drop;
    /* The launcher being dragged over this dock, empty for a running app */
    std::string dragged_launcher;

    // needed as a workaround to shrink down when removing items
    WfOption<int> height{DOCK_SECTION "/minimal_height"};
    WfOption<int> width{DOCK_SECTION "/minimal_width"};

  public:
    impl(WayfireOutput *output)
    {
        this->output = output;
        window = std::unique_ptr<WayfireAutohidingWindow>(
            new WayfireAutohidingWindow(output, DOCK_SECTION));
        window->set_auto_exclusive_zone(false);
        gtk_layer_set_layer(window->gobj(), GTK_LAYER_SHELL_LAYER_TOP);

        /* "box" is the visible dock background, so it wraps launchers too */
        layout.add_css_class("box");
        box.add_css_class("toplevels");
        box.set_selection_mode(Gtk::SelectionMode::NONE);
        separator.add_css_class("dock-separator");
        trash_separator.add_css_class("dock-separator");
        layout.append(places.get_widget());
        layout.append(launchers.get_widget());
        layout.append(separator);
        layout.append(box);
        layout.append(trash_separator);
        layout.append(trash.get_widget());
        update_separator();

        window->add_css_class("wf-dock");
        setup_drop();
        window->set_child(layout);

        window->present();
        _wl_surface = gdk_wayland_surface_get_wl_surface(
            window->get_surface()->gobj());

        box.add_tick_callback([=] (Glib::RefPtr<Gdk::FrameClock> fc)
        {
            set_clickable_region();
            return true;
        });

        auto update_entries_per_line = [=] ()
        {
            if (entries_per_line == 0)
            {
                box.set_min_children_per_line(-1);
                box.set_max_children_per_line(-1);
                return;
            }

            box.set_min_children_per_line(entries_per_line);
            box.set_max_children_per_line(entries_per_line);
        };
        entries_per_line.set_callback(update_entries_per_line);
        update_entries_per_line();

        auto update_position = [=] ()
        {
            bool vertical = position.value() == "left" || position.value() == "right";
            auto main_axis  = vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL;
            auto cross_axis = vertical ? Gtk::Orientation::HORIZONTAL : Gtk::Orientation::VERTICAL;
            layout.set_orientation(main_axis);
            launchers.set_orientation(main_axis);
            places.set_dock_position(position);
            trash.set_dock_position(position);
            separator.set_orientation(cross_axis);
            trash_separator.set_orientation(cross_axis);

            if (position.value() == "bottom")
            {
                // this is not great, but we lack better options without doing a
                // layout with boxes in boxes (ugly) or some sort of custom layout manager
                box.set_orientation(Gtk::Orientation::HORIZONTAL);
                box.set_direction(Gtk::TextDirection::LTR);
            } else if (position.value() == "left")
            {
                box.set_orientation(Gtk::Orientation::VERTICAL);
                box.set_direction(Gtk::TextDirection::LTR);
            } else if (position.value() == "right")
            {
                box.set_orientation(Gtk::Orientation::VERTICAL);
                box.set_direction(Gtk::TextDirection::RTL);
            } else // top
            {
                box.set_orientation(Gtk::Orientation::HORIZONTAL);
                box.set_direction(Gtk::TextDirection::LTR);
            }
        };
        position.set_callback(update_position);
        update_position();
    }

    /* Dropping an app among the launchers pins it there or moves it there;
     * dropping a launcher among the running apps unpins it */
    void setup_drop()
    {
        drop = Gtk::DropTarget::create(wf_dock_drag_data_get_type(), Gdk::DragAction::MOVE);
        drop->set_preload(true);
        drop->signal_motion().connect([this] (double x, double y)
        {
            auto data = dragged();
            if (!data)
            {
                return Gdk::DragAction::MOVE;
            }

            bool pinning = over_launchers(x, y);
            if (pinning)
            {
                launchers.show_drop_marker(x, y, layout, first_running());
            } else
            {
                launchers.hide_drop_marker();
            }

            dragged_launcher = data->pinned ? data->name : "";
            if (!dragged_launcher.empty())
            {
                launchers.set_unpinning(dragged_launcher, !pinning);
            }

            return Gdk::DragAction::MOVE;
        }, false);
        drop->signal_leave().connect([this] ()
        {
            launchers.hide_drop_marker();
            if (!dragged_launcher.empty())
            {
                /* Letting go outside the dock unpins it */
                launchers.set_unpinning(dragged_launcher, true);
            }
        });
        drop->signal_drop().connect([this] (const Glib::ValueBase& value, double x, double y)
        {
            auto data = (WfDockDragData*)g_value_get_boxed(value.gobj());
            launchers.hide_drop_marker();
            dragged_launcher.clear();
            if (!data)
            {
                return false;
            }

            std::string name = data->name;
            if (over_launchers(x, y))
            {
                auto before = launchers.show_drop_marker(x, y, layout, nullptr);
                launchers.hide_drop_marker();
                launcher_config::defer([name, before] () { launcher_config::pin(name, before); });
                return true;
            }

            if (data->pinned)
            {
                launcher_config::defer([name] () { launcher_config::unpin(name); });
                return true;
            }

            /* A running app dropped among the running apps: nothing to do */
            return false;
        }, false);
        layout.add_controller(drop);
    }

    /* The button of the first running app without a launcher, or nullptr */
    Gtk::Widget *first_running()
    {
        auto child = box.get_child_at_index(0);
        return child ? child->get_child() : nullptr;
    }

    WfDockDragData *dragged()
    {
        /* The C call returns the value itself; gtkmm's get_value() a copy */
        auto value = gtk_drop_target_get_value(drop->gobj());
        return (value && G_VALUE_HOLDS(value, wf_dock_drag_data_get_type())) ?
               (WfDockDragData*)g_value_get_boxed(value) : nullptr;
    }

    /* Left of the separator (above it on a vertical dock), or anywhere while
     * the dock has no running apps, but never over the trash, where dropping a
     * launcher unpins it */
    bool over_launchers(double x, double y)
    {
        bool vertical = layout.get_orientation() == Gtk::Orientation::VERTICAL;
        if (trash.visible())
        {
            Gtk::Widget& start = trash_separator.get_visible() ?
                (Gtk::Widget&)trash_separator : trash.get_widget();
            if (auto bounds = start.compute_bounds(layout))
            {
                if (vertical ? (y >= bounds->get_y()) : (x >= bounds->get_x()))
                {
                    return false;
                }
            }
        }

        if (!separator.get_visible())
        {
            return true;
        }

        auto bounds = separator.compute_bounds(layout);
        if (!bounds)
        {
            return true;
        }

        return vertical ? (y < bounds->get_center().get_y()) : (x < bounds->get_center().get_x());
    }

    void add_child(Gtk::Widget& widget)
    {
        box.append(widget);
        update_separator();
    }

    void rem_child(Gtk::Widget& widget)
    {
        /* The drop marker may be on this button */
        launchers.hide_drop_marker();
        box.remove(widget);
        update_separator();
        window->set_default_size(width, height);
    }

    void handle_config_reload()
    {
        places.reload();
        trash.reload();
        apps.reload_launchers();
        update_separator();
    }

    WfDockApps& get_apps()
    {
        return apps;
    }

    /* Only separate launchers and places from running apps when both are
     * present, and the trash from whatever is in front of it */
    void update_separator()
    {
        bool pinned  = places.visible() || !launchers.empty();
        bool running = box.get_first_child();
        separator.set_visible(pinned && running);
        trash_separator.set_visible(trash.visible() && (pinned || running));
    }

    wl_surface *get_wl_surface()
    {
        return this->_wl_surface;
    }

    /* Sets the central section as clickable and transparent edges as click-through
     * Gets called regularly to ensure css size changes all register */
    void set_clickable_region()
    {
        auto surface = window->get_surface();
        auto widget_bounds = layout.compute_bounds(*window);

        auto rect = Cairo::RectangleInt{
            (int)widget_bounds->get_x(),
            (int)widget_bounds->get_y(),
            (int)widget_bounds->get_width(),
            (int)widget_bounds->get_height()
        };

        auto region = Cairo::Region::create(rect);

        surface->set_input_region(region);
    }
};

WfDock::WfDock(WayfireOutput *output) :
    pimpl(new impl(output))
{}
WfDock::~WfDock() = default;

void WfDock::add_child(Gtk::Widget& w)
{
    return pimpl->add_child(w);
}

void WfDock::rem_child(Gtk::Widget& w)
{
    return pimpl->rem_child(w);
}

void WfDock::handle_config_reload()
{
    return pimpl->handle_config_reload();
}

WfDockApps& WfDock::get_apps()
{
    return pimpl->get_apps();
}

wl_surface*WfDock::get_wl_surface()
{
    return pimpl->get_wl_surface();
}
