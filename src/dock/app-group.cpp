#include <gdkmm/display.h>
#include <gdkmm/seat.h>
#include <giomm/dbusconnection.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gesturelongpress.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/native.h>
#include <gtkmm/separator.h>
#include <gtkmm/widgetpaintable.h>
#include <gtkmm/window.h>
#include <gdk/wayland/gdkwayland.h>
#include <wf-autohide-window.hpp>
#include <algorithm>
#include <iostream>

#include "app-group.hpp"
#include "launchers.hpp"
#include "pin.hpp"
#include "toplevel.hpp"
#include "gtk-utils.hpp"
#include "wf-shell-app.hpp"
#include "wf-option-wrap.hpp"

static std::string get_dock_option(const std::string& name)
{
    auto section = WayfireShellApp::get().config.get_section(DOCK_SECTION);
    auto option  = section ? section->get_option_or(name) : nullptr;
    return option ? option->get_value_str() : "";
}

Glib::RefPtr<Gio::DesktopAppInfo> desktop_info_for_app_id(const std::string& app_id)
{
    if (app_id.empty())
    {
        return {};
    }

    gchar *lower_cstr = g_ascii_strdown(app_id.c_str(), -1);
    std::string lower = lower_cstr;
    g_free(lower_cstr);

    for (auto& id : {app_id, lower})
    {
        for (auto& suffix : {"", ".desktop"})
        {
            auto info = Gio::DesktopAppInfo::create(id + suffix);
            if (info)
            {
                return info;
            }
        }
    }

    for (auto& info : Gio::AppInfo::get_all())
    {
        auto desktop_info = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(info);
        if (desktop_info &&
            (g_ascii_strcasecmp(desktop_info->get_startup_wm_class().c_str(), app_id.c_str()) == 0))
        {
            return desktop_info;
        }
    }

    return {};
}

/* Keep in sync with the launch-bounce animation in default.css (1 x 550ms) */
static constexpr int LAUNCH_BOUNCE_MS = 550;

WfDockAppGroup::WfDockAppGroup()
{
    button.set_child(image);
    button.add_css_class("flat");
    button.add_css_class("toplevel-icon");
    button.set_focusable(false);

    /* Left click starts the app or cycles through its windows, middle click
     * starts a new instance, right click or a long press lists the windows.
     * The button's own gesture claims left clicks, so a second gesture never
     * sees their release; left clicks come from signal_clicked instead. */
    button.signal_clicked().connect([this] ()
    {
        if (!long_pressed)
        {
            on_click(GDK_BUTTON_PRIMARY);
        }
    });

    /* The capture phase sees every press before any gesture claims it */
    click = Gtk::GestureClick::create();
    click->set_button(0);
    click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    click->signal_pressed().connect([this] (int, double, double)
    {
        long_pressed = false;
    });
    click->signal_released().connect([this] (int, double, double)
    {
        auto mouse_button = click->get_current_button();
        if (mouse_button != GDK_BUTTON_PRIMARY)
        {
            on_click(mouse_button);
        }
    });

    auto long_press = Gtk::GestureLongPress::create();
    auto long_press_ptr = long_press.get();
    long_press->signal_pressed().connect([this, long_press_ptr] (double, double)
    {
        if (windows.empty())
        {
            return;
        }

        long_pressed = true;
        long_press_ptr->set_state(Gtk::EventSequenceState::CLAIMED);
        show_windows();
    });
    button.add_controller(long_press);
    button.add_controller(click);

    list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    list.add_css_class("app-windows-list");
    list.signal_row_activated().connect([this] (Gtk::ListBoxRow *row)
    {
        activate_row(row->get_index());
    });

    /* Without a header function, the list drops headers set on its rows */
    list.set_header_func([] (Gtk::ListBoxRow *row, Gtk::ListBoxRow *before)
    {
        if (before && (row->has_css_class("new-window") || row->has_css_class("close-all")))
        {
            if (!row->get_header())
            {
                row->set_header(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
            }
        } else
        {
            row->unset_header();
        }
    });

    /* 1-9 pick a row directly, like in the places popover; closing all windows
     * has no number, so a stray key press can't do it */
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this] (guint keyval, guint, Gdk::ModifierType)
    {
        if ((keyval >= GDK_KEY_1) && (keyval <= GDK_KEY_9))
        {
            size_t index = keyval - GDK_KEY_1;
            if ((index < list_rows.size()) &&
                (list_rows[index].kind != ListRow::Kind::CLOSE_ALL))
            {
                activate_row(index);
            }

            return true;
        }

        return false;
    }, false);
    popover.add_controller(keys);

    popover.set_child(list);
    popover.add_css_class("app-windows-popover");
    popover.set_has_arrow(true);
    popover.signal_show().connect([this] ()
    {
        set_popover_open(true);
        if (auto row = list.get_row_at_index(0))
        {
            list.select_row(*row);
            row->grab_focus();
        }
    });
    popover.signal_closed().connect([this] () { set_popover_open(false); });
    gtk_widget_set_parent(GTK_WIDGET(popover.gobj()), GTK_WIDGET(button.gobj()));
}

WfDockAppGroup::~WfDockAppGroup()
{
    launching_timeout.disconnect();
    gtk_widget_unparent(GTK_WIDGET(popover.gobj()));
}

Gtk::Button& WfDockAppGroup::get_widget()
{
    return button;
}

Gtk::Image& WfDockAppGroup::get_image()
{
    return image;
}

void WfDockAppGroup::set_app_info(Glib::RefPtr<Gio::AppInfo> app_info)
{
    this->app_info = app_info;
    update();
}

Glib::RefPtr<Gio::AppInfo> WfDockAppGroup::get_app_info() const
{
    return app_info;
}

void WfDockAppGroup::set_pin(const std::string& name, bool pinned)
{
    pin_name     = name;
    this->pinned = pinned;
    if (drag)
    {
        return;
    }

    /* A drag only starts once the pointer moved a few pixels with the button
     * held, so clicks and long presses work as before. The dock is the drop
     * target; a launcher dropped anywhere else is unpinned. */
    drag = Gtk::DragSource::create();
    drag->set_actions(Gdk::DragAction::MOVE);
    drag->signal_prepare().connect([this] (double, double)
    {
        WfDockDragData data{(gchar*)pin_name.c_str(), this->pinned};
        Glib::ValueBase value;
        value.init(wf_dock_drag_data_get_type());
        g_value_set_boxed(value.gobj(), &data);
        return Gdk::ContentProvider::create(value);
    }, false);
    drag->signal_drag_begin().connect([this] (const Glib::RefPtr<Gdk::Drag>&)
    {
        drag->set_icon(Gtk::WidgetPaintable::create(image),
            image.get_width() / 2, image.get_height() / 2);
        button.add_css_class("dragging");
    });
    /* The group can be gone by the time the drag ends, e.g. when the app closed
     * its last window meanwhile; GTK keeps the drag source alive until then */
    std::weak_ptr<bool> alive = this->alive;
    drag->signal_drag_end().connect([this, alive] (const Glib::RefPtr<Gdk::Drag>&, bool dropped)
    {
        std::string name;
        bool unpin = false;
        if (alive.lock())
        {
            button.remove_css_class("dragging");
            button.remove_css_class("unpinning");
            name  = pin_name;
            unpin = !dropped && this->pinned;
        }

        /* After this handler returns: the change rebuilds the buttons */
        Glib::signal_idle().connect_once([name, unpin] ()
        {
            if (unpin)
            {
                /* Dropped outside the dock */
                launcher_config::unpin(name);
            } else
            {
                launcher_config::run_deferred();
            }
        });
    });
    button.add_controller(drag);
}

const std::string& WfDockAppGroup::get_pin_name() const
{
    return pin_name;
}

void WfDockAppGroup::add_window(WfDockWindow *window)
{
    windows.push_back(window);
    update();
}

void WfDockAppGroup::remove_window(WfDockWindow *window)
{
    windows.erase(std::remove(windows.begin(), windows.end(), window), windows.end());
    update();
}

bool WfDockAppGroup::empty() const
{
    return windows.empty();
}

static void set_css_class(Gtk::Widget& widget, const std::string& css_class, bool set)
{
    if (set)
    {
        widget.add_css_class(css_class);
    } else
    {
        widget.remove_css_class(css_class);
    }
}

void WfDockAppGroup::update()
{
    bool activated = false, all_minimized = !windows.empty();
    for (auto window : windows)
    {
        activated     |= window->state & WF_TOPLEVEL_STATE_ACTIVATED;
        all_minimized &= bool(window->state & WF_TOPLEVEL_STATE_MINIMIZED);
    }

    set_css_class(button, "running", !windows.empty());
    set_css_class(button, "multiple", windows.size() > 1);
    set_css_class(button, "activated", activated);
    set_css_class(button, "minimized", all_minimized);

    if (app_info)
    {
        button.set_tooltip_text(app_info->get_display_name());
    } else if (auto window = most_recent())
    {
        button.set_tooltip_text(window->title);
    }

    if (popover.is_visible())
    {
        if (windows.empty())
        {
            popover.popdown();
        } else
        {
            rebuild_list();
        }
    }
}

void WfDockAppGroup::on_click(int mouse_button)
{
    if (mouse_button == GDK_BUTTON_MIDDLE)
    {
        launch_new_window();
        return;
    }

    if (mouse_button == GDK_BUTTON_SECONDARY)
    {
        if (!windows.empty())
        {
            show_windows();
        }

        return;
    }

    if (mouse_button != GDK_BUTTON_PRIMARY)
    {
        return;
    }

    if (windows.empty())
    {
        launch();
        return;
    }

    auto active = std::find_if(windows.begin(), windows.end(), [] (WfDockWindow *window)
    {
        return window->state & WF_TOPLEVEL_STATE_ACTIVATED;
    });

    if (active == windows.end())
    {
        activate(most_recent());
    } else if (windows.size() == 1)
    {
        toggle_minimized(*active);
    } else
    {
        /* Cycle in the order the windows were opened; focus changes would
         * reorder a most-recently-used list and flip between two windows */
        auto next = std::next(active);
        activate(next == windows.end() ? windows.front() : *next);
    }
}

void WfDockAppGroup::activate(WfDockWindow *window)
{
    if (window->state & WF_TOPLEVEL_STATE_MINIMIZED)
    {
        send_rectangle_hint(window);
        zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
    }

    auto gseat = Gdk::Display::get_default()->get_default_seat();
    auto seat  = gdk_wayland_seat_get_wl_seat(gseat->gobj());
    zwlr_foreign_toplevel_handle_v1_activate(window->handle, seat);
}

void WfDockAppGroup::toggle_minimized(WfDockWindow *window)
{
    send_rectangle_hint(window);
    if (window->state & WF_TOPLEVEL_STATE_MINIMIZED)
    {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
    } else
    {
        zwlr_foreign_toplevel_handle_v1_set_minimized(window->handle);
    }
}

/* Tells the compositor where to animate the window to and from when minimizing */
void WfDockAppGroup::send_rectangle_hint(WfDockWindow *window)
{
    auto root   = dynamic_cast<Gtk::Widget*>(button.get_root());
    auto native = button.get_native();
    if (!root || !native || !native->get_surface())
    {
        return;
    }

    auto bounds = button.compute_bounds(*root);
    if (!bounds)
    {
        return;
    }

    zwlr_foreign_toplevel_handle_v1_set_rectangle(window->handle,
        gdk_wayland_surface_get_wl_surface(native->get_surface()->gobj()),
        bounds->get_x(), bounds->get_y(), bounds->get_width(), bounds->get_height());
}

/* The last activated window; newer windows win among those never activated */
WfDockWindow*WfDockAppGroup::most_recent() const
{
    WfDockWindow *result = nullptr;
    for (auto window : windows)
    {
        if (!result || (window->focus_serial >= result->focus_serial))
        {
            result = window;
        }
    }

    return result;
}

void WfDockAppGroup::show_windows()
{
    auto position = get_dock_option("position");
    if (position == "top")
    {
        popover.set_position(Gtk::PositionType::BOTTOM);
    } else if (position == "left")
    {
        popover.set_position(Gtk::PositionType::RIGHT);
    } else if (position == "right")
    {
        popover.set_position(Gtk::PositionType::LEFT);
    } else
    {
        popover.set_position(Gtk::PositionType::TOP);
    }

    /* Fill the list before showing: GtkPopover moves the focus into its content
     * on show, before any show handler of ours runs, and passes NULL to
     * gtk_widget_is_ancestor() when an empty list leaves nothing to focus */
    rebuild_list();
    popover.popup();
}

/* Rows look like the places popover: label and the 1-9 shortcut */
static Gtk::Box *make_row(Gtk::Label& label, int index = 9)
{
    label.set_xalign(0);
    label.set_hexpand(true);
    label.set_max_width_chars(60);
    label.set_ellipsize(Pango::EllipsizeMode::END);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->append(label);
    if (index < 9)
    {
        auto key = Gtk::make_managed<Gtk::Label>(std::to_string(index + 1));
        key->add_css_class("dim-label");
        box->append(*key);
    }

    return box;
}

void WfDockAppGroup::rebuild_list()
{
    while (auto row = list.get_row_at_index(0))
    {
        list.remove(*row);
    }

    list_rows.clear();
    for (auto window : windows)
    {
        auto title = window->title.empty() ? window->app_id : window->title;
        auto label = Gtk::make_managed<Gtk::Label>();
        if (window->state & WF_TOPLEVEL_STATE_ACTIVATED)
        {
            label->set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");
        } else
        {
            label->set_text(title);
        }

        if (window->state & WF_TOPLEVEL_STATE_MINIMIZED)
        {
            label->add_css_class("dim-label");
        }

        list.append(*make_row(*label, list_rows.size()));
        list_rows.push_back({ListRow::Kind::WINDOW, window->handle});
    }

    if (app_info)
    {
        auto label = Gtk::make_managed<Gtk::Label>("New Window");
        list.append(*make_row(*label, list_rows.size()));
        list.get_row_at_index(list_rows.size())->add_css_class("new-window");
        list_rows.push_back({ListRow::Kind::NEW_WINDOW});
    }

    /* Last and without a number, away from the entries clicked often */
    if (!windows.empty())
    {
        auto label = Gtk::make_managed<Gtk::Label>(windows.size() == 1 ?
            std::string("Close Window") : "Close " + std::to_string(windows.size()) + " Windows");
        list.append(*make_row(*label));
        list.get_row_at_index(list_rows.size())->add_css_class("close-all");
        list_rows.push_back({ListRow::Kind::CLOSE_ALL});
    }

    list.invalidate_headers();
}

void WfDockAppGroup::activate_row(int index)
{
    if ((index < 0) || (index >= (int)list_rows.size()))
    {
        return;
    }

    auto row = list_rows[index];
    popover.popdown();
    switch (row.kind)
    {
      case ListRow::Kind::NEW_WINDOW:
        launch_new_window();
        return;

      case ListRow::Kind::CLOSE_ALL:
        close_all_windows();
        return;

      case ListRow::Kind::WINDOW:
        break;
    }

    for (auto window : windows)
    {
        if (window->handle == row.handle)
        {
            activate(window);
            return;
        }
    }
}

/* The same request as a window's close button, so apps can still ask to save */
void WfDockAppGroup::close_all_windows()
{
    for (auto window : windows)
    {
        zwlr_foreign_toplevel_handle_v1_close(window->handle);
    }
}

/* Keeps the dock shown and lets it take keyboard input while the popover is open */
void WfDockAppGroup::set_popover_open(bool open)
{
    if (auto window = dynamic_cast<WayfireAutohidingWindow*>(button.get_root()))
    {
        window->set_popover_open(open);
    }
}

void WfDockAppGroup::launch()
{
    if (!app_info)
    {
        return;
    }

    auto ctx = Gdk::Display::get_default()->get_app_launch_context();
    try {
        app_info->launch(std::vector<Glib::RefPtr<Gio::File>>(), ctx);
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-dock-plus: failed to launch " << app_info->get_display_name() <<
            ": " << e.what() << std::endl;
        return;
    }

    bounce();
}

void WfDockAppGroup::bounce()
{
    button.add_css_class("launching");
    launching_timeout.disconnect();
    launching_timeout = Glib::signal_timeout().connect([this] ()
    {
        button.remove_css_class("launching");
        return false;
    }, LAUNCH_BOUNCE_MS);
}

/* Starting a single-instance app again only presents its existing window, so
 * prefer the desktop file's new-window action, then the running app's own
 * new-window action (what its Ctrl+Shift+N runs), then a plain launch. */
void WfDockAppGroup::launch_new_window()
{
    auto desktop_info = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(app_info);
    if (desktop_info)
    {
        auto actions = desktop_info->list_actions();
        if (std::find(actions.begin(), actions.end(), "new-window") != actions.end())
        {
            desktop_info->launch_action("new-window",
                Gdk::Display::get_default()->get_app_launch_context());
            bounce();
            return;
        }
    }

    if (windows.empty() || !activate_app_new_window())
    {
        launch();
    }
}

/* Runs app.new-window in the running app over D-Bus. GApplication exports its
 * actions under its application id, which by convention is the desktop id.
 * Returns false if that can't even be tried; falls back to launch() if the
 * app turns out not to have the action. */
bool WfDockAppGroup::activate_app_new_window()
{
    auto desktop_info = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(app_info);
    std::string app_id = desktop_info ? desktop_info->get_id() : "";
    const std::string suffix = ".desktop";
    if ((app_id.size() > suffix.size()) &&
        (app_id.compare(app_id.size() - suffix.size(), suffix.size(), suffix) == 0))
    {
        app_id.resize(app_id.size() - suffix.size());
    }

    std::string path = "/" + app_id;
    std::replace(path.begin(), path.end(), '.', '/');
    std::replace(path.begin(), path.end(), '-', '_');
    if (!g_application_id_is_valid(app_id.c_str()) || !g_variant_is_object_path(path.c_str()))
    {
        return false;
    }

    Glib::RefPtr<Gio::DBus::Connection> bus;
    try {
        bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
    } catch (const Glib::Error& e)
    {
        return false;
    }

    /* Lets the compositor focus the new window, as for a normal launch */
    auto ctx   = Gdk::Display::get_default()->get_app_launch_context();
    auto token = ctx->get_startup_notify_id(app_info, {});
    std::weak_ptr<bool> alive = this->alive;

    auto fall_back = [this, alive] ()
    {
        if (alive.lock())
        {
            launch();
        }
    };

    bus->call(path, "org.gtk.Actions", "List", Glib::VariantContainerBase(),
        [=] (Glib::RefPtr<Gio::AsyncResult>& result)
    {
        std::vector<Glib::ustring> actions;
        try {
            Glib::Variant<std::vector<Glib::ustring>> list;
            bus->call_finish(result).get_child(list, 0);
            actions = list.get();
        } catch (const Glib::Error&)
        {}

        if (std::find(actions.begin(), actions.end(), "new-window") == actions.end())
        {
            fall_back();
            return;
        }

        GVariantBuilder platform_data;
        g_variant_builder_init(&platform_data, G_VARIANT_TYPE_VARDICT);
        if (!token.empty())
        {
            g_variant_builder_add(&platform_data, "{sv}", "activation-token",
                g_variant_new_string(token.c_str()));
            g_variant_builder_add(&platform_data, "{sv}", "desktop-startup-id",
                g_variant_new_string(token.c_str()));
        }

        auto params = g_variant_ref_sink(g_variant_new("(sava{sv})", "new-window",
            nullptr, &platform_data));
        bus->call(path, "org.freedesktop.Application", "ActivateAction",
            Glib::VariantContainerBase(params, false),
            [=] (Glib::RefPtr<Gio::AsyncResult>& result)
        {
            try {
                bus->call_finish(result);
            } catch (const Glib::Error& e)
            {
                std::cerr << "wf-dock-plus: " << app_id << " didn't open a new window: " <<
                    e.what() << std::endl;
                fall_back();
                return;
            }

            if (alive.lock())
            {
                bounce();
            }
        }, app_id, 2000);
    }, app_id, 2000);

    return true;
}

WfDockApps::WfDockApps(WfDockLaunchers& launchers, WidgetCallback add, WidgetCallback remove) :
    launchers(launchers), add_widget(add), remove_widget(remove)
{}

WfDockApps::~WfDockApps()
{
    for (auto& [handle, window] : windows)
    {
        if (window.group)
        {
            window.group->remove_window(&window);
        }
    }

    for (auto& [key, icon] : icons)
    {
        icon.removal.disconnect();
        remove_widget(icon.group->get_widget());
    }
}

void WfDockApps::set_window(zwlr_foreign_toplevel_handle_v1 *handle,
    const std::string& app_id, const std::string& title, uint32_t state)
{
    auto [it, inserted] = windows.try_emplace(handle);
    auto& window = it->second;
    window.handle = handle;

    if (window.group && (window.app_id != app_id))
    {
        detach(window, false);
    }

    if ((state & WF_TOPLEVEL_STATE_ACTIVATED) && !(window.state & WF_TOPLEVEL_STATE_ACTIVATED))
    {
        window.focus_serial = ++focus_serial;
    }

    window.app_id = app_id;
    window.title  = title;
    window.state  = state;
    if (window.group)
    {
        window.group->update();
    } else
    {
        window.group = group_for(window);
        window.group->add_window(&window);
    }
}

void WfDockApps::remove_window(zwlr_foreign_toplevel_handle_v1 *handle, bool closing)
{
    auto it = windows.find(handle);
    if (it == windows.end())
    {
        return;
    }

    if (it->second.group)
    {
        detach(it->second, closing);
    }

    windows.erase(it);
}

void WfDockApps::reload_launchers()
{
    /* The launchers and their window lists are about to be destroyed */
    for (auto& [handle, window] : windows)
    {
        bool in_icon = std::any_of(icons.begin(), icons.end(), [&] (auto& entry)
        {
            return entry.second.group.get() == window.group;
        });
        if (!in_icon)
        {
            window.group = nullptr;
        }
    }

    launchers.reload();
    for (auto& [handle, window] : windows)
    {
        auto launcher = launchers.find(window.app_id);
        if (launcher && window.group)
        {
            detach(window, false);
        }

        if (!window.group)
        {
            window.group = launcher ? launcher : group_for(window);
            window.group->add_window(&window);
        }
    }
}

WfDockAppGroup*WfDockApps::group_for(const WfDockWindow& window)
{
    if (auto launcher = launchers.find(window.app_id))
    {
        return launcher;
    }

    auto key = window.app_id.empty() ?
        "\n" + std::to_string((uintptr_t)window.handle) : window.app_id;
    auto& icon = icons[key];
    if (icon.group)
    {
        /* A new window of an app whose icon is still playing the close animation */
        icon.removal.disconnect();
        icon.group->get_widget().remove_css_class("closing");
        return icon.group.get();
    }

    icon.group = std::make_unique<WfDockAppGroup>();
    auto info = desktop_info_for_app_id(window.app_id);
    if (info && info->get_icon())
    {
        icon.group->get_image().set(info->get_icon());
    } else
    {
        IconProvider::image_set_icon(icon.group->get_image(), window.app_id);
    }

    icon.group->set_app_info(info);
    if (info)
    {
        /* Pinning stores the desktop id without the .desktop suffix */
        auto id = info->get_id();
        const std::string suffix = ".desktop";
        if ((id.size() > suffix.size()) &&
            (id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0))
        {
            id.resize(id.size() - suffix.size());
        }

        icon.group->set_pin(id, false);
    }

    add_widget(icon.group->get_widget());
    return icon.group.get();
}

void WfDockApps::detach(WfDockWindow& window, bool closing)
{
    auto group = window.group;
    group->remove_window(&window);
    window.group = nullptr;
    if (!group->empty())
    {
        return;
    }

    auto icon = std::find_if(icons.begin(), icons.end(), [&] (auto& entry)
    {
        return entry.second.group.get() == group;
    });
    if (icon == icons.end())
    {
        return;
    }

    auto key = icon->first;
    WfOption<bool> show_close{DOCK_SECTION "/show_close"};
    if (closing && show_close)
    {
        /* Keep the icon for the close animation, like the window icons upstream */
        group->get_widget().add_css_class("closing");
        icon->second.removal = Glib::signal_timeout().connect([this, key] ()
        {
            remove_icon(key);
            return false;
        }, 2000);
    } else
    {
        remove_icon(key);
    }
}

void WfDockApps::remove_icon(const std::string& key)
{
    auto it = icons.find(key);
    if (it == icons.end())
    {
        return;
    }

    it->second.removal.disconnect();
    remove_widget(it->second.group->get_widget());
    icons.erase(it);
}
