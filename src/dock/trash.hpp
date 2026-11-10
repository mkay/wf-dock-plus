#pragma once

#include <string>
#include <giomm/file.h>
#include <giomm/filemonitor.h>
#include <gtkmm/button.h>
#include <gtkmm/droptargetasync.h>
#include <gtkmm/image.h>
#include <gtkmm/listbox.h>
#include <gtkmm/popover.h>

/* A dock button for the freedesktop.org trash, through GIO's trash:/// (gvfs).
 * Click opens it in the file manager, files dropped on it are trashed, and the
 * right-click menu empties it. The icon switches between empty and full. */
class WfDockTrash
{
  public:
    WfDockTrash();
    ~WfDockTrash();

    Gtk::Button& get_widget();
    bool visible() const;
    /* Opens the popover away from the screen edge given by dock/position */
    void set_dock_position(const std::string& position);
    void reload();

  private:
    Gtk::Button button;
    Gtk::Image image;
    Gtk::Popover popover;
    Gtk::ListBox list;
    Glib::RefPtr<Gtk::DropTargetAsync> drop;

    Glib::RefPtr<Gio::File> trash;
    Glib::RefPtr<Gio::FileMonitor> monitor;
    Glib::RefPtr<Gio::Cancellable> count_query;
    sigc::connection count_timeout;
    guint64 count = 0;
    /* Whether trash:/// works, i.e. gvfs is running */
    bool available = false;
    /* The menu asks before emptying */
    bool confirming = false;

    void start_monitor();
    void schedule_count_update();
    void update_count();
    void update_icon();
    void rebuild_menu();
    void activate(int index);
    void open();
    void empty();
    void set_popover_open(bool open);
};
