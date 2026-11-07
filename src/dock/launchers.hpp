#pragma once

#include <memory>
#include <string>
#include <vector>
#include <gtkmm/box.h>

#include "app-group.hpp"

/* A single pinned application in the dock. Clicking it starts the app,
 * or switches to its windows while it is running. */
class WfDockLauncher
{
  public:
    WfDockAppGroup group;

    /* Resolves name to a desktop entry, or to the launcher_cmd_<name> entry
     * in the [dock] section. Returns false if nothing could be found. */
    bool initialize(const std::string& name);
    /* Whether windows with this app_id belong to the launcher */
    bool matches(const std::string& app_id) const;

  private:
    std::vector<std::string> app_ids;

    void add_app_id(const std::string& app_id);
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
    /* The launcher for windows with this app_id, or nullptr */
    WfDockAppGroup *find(const std::string& app_id);

  private:
    Gtk::Box box;
    std::vector<std::unique_ptr<WfDockLauncher>> launchers;
};
