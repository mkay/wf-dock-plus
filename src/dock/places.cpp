#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glibmm/convert.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <gdkmm/display.h>
#include <gtkmm/box.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>
#include <gtk4-layer-shell.h>
#include <wayfire/config/types.hpp>
#include <iostream>

#include "places.hpp"
#include "gtk-utils.hpp"
#include "wf-shell-app.hpp"

static std::string get_dock_option(const std::string& name)
{
    auto section = WayfireShellApp::get().config.get_section("dock");
    auto option  = section ? section->get_option_or(name) : nullptr;
    return option ? option->get_value_str() : "";
}

WfDockPlaces::WfDockPlaces()
{
    button.set_child(image);
    button.set_always_show_arrow(false);
    button.set_has_frame(false);
    button.add_css_class("toplevel-icon");
    button.add_css_class("launcher");
    button.add_css_class("places");
    button.set_focusable(false);
    /* The inner toggle button would otherwise take focus on startup and after
     * every popdown, and the theme draws a focus frame around it */
    button.get_first_child()->set_focusable(false);

    list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    list.add_css_class("places-list");
    list.signal_row_activated().connect([this] (Gtk::ListBoxRow *row)
    {
        open(row->get_index());
    });
    popover.set_child(list);
    popover.add_css_class("places-popover");

    /* 1-9 open a row directly; arrows and Enter are handled by the list */
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this] (guint keyval, guint, Gdk::ModifierType)
    {
        if ((keyval >= GDK_KEY_1) && (keyval <= GDK_KEY_9))
        {
            int index = keyval - GDK_KEY_1;
            if (index < (int)places.size())
            {
                open(index);
            }

            return true;
        }

        return false;
    }, false);
    popover.add_controller(keys);

    /* Rebuilt on every popup so bookmark edits show up without a reload */
    popover.signal_show().connect([this] ()
    {
        rebuild();
        set_keyboard_grab(true);
        if (auto row = list.get_row_at_index(0))
        {
            list.select_row(*row);
            row->grab_focus();
        }
    });
    popover.signal_closed().connect([this] () { set_keyboard_grab(false); });
    button.set_popover(popover);
    reload();
}

Gtk::MenuButton& WfDockPlaces::get_widget()
{
    return button;
}

bool WfDockPlaces::visible() const
{
    return button.get_visible();
}

void WfDockPlaces::set_dock_position(const std::string& position)
{
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
}

void WfDockPlaces::reload()
{
    auto enabled = wf::option_type::from_string<bool>(get_dock_option("places_button"));
    button.set_visible(enabled.value_or(false));

    auto icon = get_dock_option("places_icon");
    IconProvider::image_set_icon(image, icon.empty() ? "folder" : icon);
    auto label = get_dock_option("places_label");
    button.set_tooltip_text(label.empty() ? "Places" : label);
}

/* The dock surface takes no keyboard input; it has to while the popover is open */
void WfDockPlaces::set_keyboard_grab(bool grab)
{
    auto window = dynamic_cast<Gtk::Window*>(button.get_root());
    if (window)
    {
        gtk_layer_set_keyboard_mode(window->gobj(), grab ?
            GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE : GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    }
}

/* Accepts ~, ~/path, /path and URIs. Paths with spaces must be given as file:// URIs. */
void WfDockPlaces::add_place(const std::string& entry)
{
    if (entry == "bookmarks")
    {
        add_bookmarks();
        return;
    }

    auto home = Glib::get_home_dir();
    std::string path = entry;
    if ((path == "~") || (path.rfind("~/", 0) == 0))
    {
        path = home + path.substr(1);
    }

    gchar *scheme = g_uri_parse_scheme(path.c_str());
    bool is_uri   = scheme;
    g_free(scheme);

    Glib::RefPtr<Gio::File> file;
    if (path[0] == '/')
    {
        file = Gio::File::create_for_path(path);
    } else if (!is_uri)
    {
        std::cerr << "wf-dock: ignoring place \"" << entry <<
            "\": not a path or URI" << std::endl;
        return;
    } else
    {
        file = Gio::File::create_for_uri(path);
    }

    std::string label = file->get_basename();
    if (file->get_path() == home)
    {
        label = "Home";
    }

    places.push_back({label, file->get_uri()});
}

/* GTK bookmarks, as shown in the Nautilus sidebar: "uri [label]" per line */
void WfDockPlaces::add_bookmarks()
{
    auto bookmarks = Glib::build_filename(Glib::get_user_config_dir(), "gtk-3.0", "bookmarks");
    std::string contents;
    try {
        contents = Glib::file_get_contents(bookmarks);
    } catch (const Glib::Error&)
    {
        return;
    }

    for (auto& line : Glib::Regex::split_simple("\n", contents.c_str()))
    {
        if (line.empty())
        {
            continue;
        }

        auto space = line.find(' ');
        std::string uri   = line.substr(0, space);
        std::string label = (space == std::string::npos) ?
            Gio::File::create_for_uri(uri)->get_basename() : std::string(line.substr(space + 1));
        places.push_back({label, uri});
    }
}

/* dock/places lists the folders in order; empty means Home plus the GTK bookmarks */
void WfDockPlaces::rebuild()
{
    places.clear();
    auto entries = get_dock_option("places");
    if (entries.find_first_not_of(" \t") == std::string::npos)
    {
        entries = "~ bookmarks";
    }

    for (auto& entry : Glib::Regex::split_simple("\\s+", entries.c_str()))
    {
        if (!entry.empty())
        {
            add_place(entry);
        }
    }

    while (auto row = list.get_row_at_index(0))
    {
        list.remove(*row);
    }

    for (size_t i = 0; i < places.size(); i++)
    {
        auto box  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("folder");
        try {
            auto info = Gio::File::create_for_uri(places[i].uri)->query_info("standard::icon");
            if (info->get_icon())
            {
                icon->set(info->get_icon());
            }
        } catch (const Glib::Error&)
        {}

        auto label = Gtk::make_managed<Gtk::Label>(places[i].label);
        label->set_xalign(0);
        label->set_hexpand(true);
        box->append(*icon);
        box->append(*label);
        if (i < 9)
        {
            auto key = Gtk::make_managed<Gtk::Label>(std::to_string(i + 1));
            key->add_css_class("dim-label");
            box->append(*key);
        }

        list.append(*box);
    }
}

void WfDockPlaces::open(int index)
{
    auto uri = places[index].uri;
    popover.popdown();
    auto ctx = Gdk::Display::get_default()->get_app_launch_context();

    auto cmd = get_dock_option("places_file_manager");
    if (!cmd.empty())
    {
        /* GIO appends the folder URI to a command line without field codes */
        try {
            auto app = Gio::AppInfo::create_from_commandline(cmd, "",
                Gio::AppInfo::CreateFlags::SUPPORTS_URIS);
            app->launch_uri(uri, ctx);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock: failed to run \"" << cmd << "\" for " << uri <<
                ": " << e.what() << std::endl;
        }

        return;
    }

    Gio::AppInfo::launch_default_for_uri_async(uri, ctx, [uri] (Glib::RefPtr<Gio::AsyncResult>& result)
    {
        try {
            Gio::AppInfo::launch_default_for_uri_finish(result);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock: failed to open " << uri << ": " << e.what() << std::endl;
        }
    });
}
