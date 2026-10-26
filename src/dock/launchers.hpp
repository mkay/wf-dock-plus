#pragma once

#include <memory>
#include <string>
#include <vector>
#include <giomm/appinfo.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <sigc++/connection.h>

/* A single pinned application in the dock. Clicking it starts a new instance. */
class WfDockLauncher
{
  public:
    Gtk::Button button;

    ~WfDockLauncher();

    /* Resolves name to a desktop entry, or to the launcher_cmd_<name> entry
     * in the [dock] section. Returns false if nothing could be found. */
    bool initialize(const std::string& name);

  private:
    Gtk::Image image;
    Glib::RefPtr<Gio::AppInfo> app_info;
    sigc::connection launching_timeout;

    void launch();
};

/* The row of pinned applications, in the order given by dock/launchers */
class WfDockLaunchers
{
  public:
    WfDockLaunchers();

    Gtk::Box& get_widget();
    bool empty() const;
    void set_orientation(Gtk::Orientation orientation);
    void reload();

  private:
    Gtk::Box box;
    std::vector<std::unique_ptr<WfDockLauncher>> launchers;
};
