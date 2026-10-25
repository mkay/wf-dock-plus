#pragma once

#include <string>
#include <vector>
#include <gtkmm/image.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>

/* A dock button that pops up a list of folders and opens the chosen one
 * in the file manager. The list comes from dock/places. */
class WfDockPlaces
{
  public:
    WfDockPlaces();

    Gtk::MenuButton& get_widget();
    bool visible() const;
    /* Opens the popover away from the screen edge given by dock/position */
    void set_dock_position(const std::string& position);
    void reload();

  private:
    struct Place
    {
        std::string label;
        std::string uri;
    };

    Gtk::MenuButton button;
    Gtk::Image image;
    Gtk::Popover popover;
    Gtk::ListBox list;
    std::vector<Place> places;

    void rebuild();
    void add_place(const std::string& entry);
    void add_bookmarks();
    void open(int index);
    void set_keyboard_grab(bool grab);
};
