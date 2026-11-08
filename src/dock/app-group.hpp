#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <giomm/appinfo.h>
#include <giomm/desktopappinfo.h>
#include <gtkmm/button.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/image.h>
#include <gtkmm/listbox.h>
#include <gtkmm/popover.h>
#include <sigc++/connection.h>
#include <wlr-foreign-toplevel-management-unstable-v1-client-protocol.h>

class WfDockLaunchers;

/* The desktop entry for a window's app_id, matched by desktop id or StartupWMClass */
Glib::RefPtr<Gio::DesktopAppInfo> desktop_info_for_app_id(const std::string& app_id);

/* A window shown on one dock */
struct WfDockWindow
{
    zwlr_foreign_toplevel_handle_v1 *handle;
    std::string app_id;
    std::string title;
    uint32_t state = 0;
    /* Increases every time the window is activated; 0 if it never was */
    uint64_t focus_serial = 0;
    class WfDockAppGroup *group = nullptr;
};

/* One dock button for an app: a launcher, or a running app without one.
 * It starts the app, cycles through its windows and lists them in a popover. */
class WfDockAppGroup
{
  public:
    WfDockAppGroup();
    ~WfDockAppGroup();

    Gtk::Button& get_widget();
    Gtk::Image& get_image();
    /* Used to start new instances and as the tooltip; may be empty */
    void set_app_info(Glib::RefPtr<Gio::AppInfo> app_info);
    Glib::RefPtr<Gio::AppInfo> get_app_info() const;
    /* Makes the button draggable, to pin, move or unpin it. name is the
     * launcher name, or for a running app its desktop id */
    void set_pin(const std::string& name, bool pinned);
    const std::string& get_pin_name() const;

    void add_window(WfDockWindow *window);
    void remove_window(WfDockWindow *window);
    bool empty() const;
    /* Call after a window's title or state changed */
    void update();

    /* Starts the app; does nothing for an already running single-instance app */
    void launch();
    /* Opens a new window, also for single-instance apps */
    void launch_new_window();

  private:
    Gtk::Button button;
    Gtk::Image image;
    Gtk::Popover popover;
    Gtk::ListBox list;
    Glib::RefPtr<Gtk::GestureClick> click;
    Glib::RefPtr<Gtk::DragSource> drag;
    std::string pin_name;
    bool pinned = false;
    Glib::RefPtr<Gio::AppInfo> app_info;
    /* In the order they were opened, which is the order clicks cycle through */
    std::vector<WfDockWindow*> windows;
    /* What each row of the window list does, in row order */
    struct ListRow
    {
        enum class Kind { WINDOW, NEW_WINDOW, CLOSE_ALL } kind;
        zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
    };
    std::vector<ListRow> list_rows;
    sigc::connection launching_timeout;
    bool long_pressed = false;
    /* Lets asynchronous D-Bus replies check that the group still exists */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void on_click(int button);
    void activate(WfDockWindow *window);
    void toggle_minimized(WfDockWindow *window);
    void send_rectangle_hint(WfDockWindow *window);
    WfDockWindow *most_recent() const;
    void show_windows();
    void rebuild_list();
    void activate_row(int index);
    void close_all_windows();
    void set_popover_open(bool open);
    void bounce();
    bool activate_app_new_window();
};

/* Sorts the windows on one dock into the launchers and into one icon
 * per running app without a launcher */
class WfDockApps
{
  public:
    using WidgetCallback = std::function<void (Gtk::Widget&)>;

    /* add/remove put the icons of apps without a launcher into the dock */
    WfDockApps(WfDockLaunchers& launchers, WidgetCallback add, WidgetCallback remove);
    ~WfDockApps();

    /* Adds the window or updates its app_id, title and state */
    void set_window(zwlr_foreign_toplevel_handle_v1 *handle, const std::string& app_id,
        const std::string& title, uint32_t state);
    /* closing: the window was closed, play the close animation if dock/show_close */
    void remove_window(zwlr_foreign_toplevel_handle_v1 *handle, bool closing);
    /* Reloads the launchers and sorts all windows again */
    void reload_launchers();

  private:
    struct AppIcon
    {
        std::unique_ptr<WfDockAppGroup> group;
        sigc::connection removal;
    };

    WfDockLaunchers& launchers;
    WidgetCallback add_widget, remove_widget;
    std::map<zwlr_foreign_toplevel_handle_v1*, WfDockWindow> windows;
    /* Keyed by app_id, or by window for windows without one */
    std::map<std::string, AppIcon> icons;
    uint64_t focus_serial = 0;

    WfDockAppGroup *group_for(const WfDockWindow& window);
    void detach(WfDockWindow& window, bool closing);
    void remove_icon(const std::string& key);
};
