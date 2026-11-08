#include "glibmm/ustring.h"
#include <gtk-utils.hpp>
#include <glibmm.h>
#include <gtkmm/icontheme.h>
#include <gdk/gdkcairo.h>
#include <iostream>
#include <giomm/desktopappinfo.h>
#include "wf-shell-app.hpp"

Glib::RefPtr<Gtk::CssProvider> load_css_from_path(std::string path)
{
    try {
        auto css = Gtk::CssProvider::create();
        css->load_from_path(path);
        return css;
    } catch (Glib::Error& err)
    {
        std::cerr << "Failed to load CSS: " << err.what() << std::endl;
        return {};
    } catch (...)
    {
        std::cerr << "Failed to load CSS at: " << path << std::endl;
        return {};
    }
}

namespace IconProvider
{
std::map<std::string, std::string> custom_icons;

std::string tolower(std::string str)
{
    for (auto& c : str)
    {
        c = std::tolower(c);
    }

    return str;
}

/* Gio::DesktopAppInfo
 *
 * Usually knowing the app_id, we can get a desktop app info from Gio
 * The filename is either the app_id + ".desktop" or lower_app_id + ".desktop" */
Glib::RefPtr<Gio::Icon> get_from_desktop_app_info(std::string app_id)
{
    std::vector<std::string> prefixes = {
        "",
        "org.kde.",
    };

    std::vector<std::string> app_id_variations = {
        app_id,
        tolower(app_id),
    };

    std::vector<std::string> suffixes = {
        "",
        ".desktop"
    };

    for (auto& prefix : prefixes)
    {
        for (auto& id : app_id_variations)
        {
            for (auto& suffix : suffixes)
            {
                auto app_info = Gio::DesktopAppInfo::create(prefix + id + suffix);
                if (app_info)
                {
                    return app_info->get_icon();
                }
            }
        }
    }

    /* Apps whose app_id differs from their desktop file name, e.g. browser web apps,
     * declare it as StartupWMClass */
    for (auto& info : Gio::AppInfo::get_all())
    {
        auto desktop_info = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(info);
        if (desktop_info && (tolower(desktop_info->get_startup_wm_class()) == tolower(app_id)))
        {
            return desktop_info->get_icon();
        }
    }

    return {};
}

bool image_set_icon(Gtk::Image & image, Glib::ustring path)
{
    if (path.empty())
    {
        return false;
    }

    if ((path.rfind("/", 0) == 0) || (path.rfind("~", 0) == 0))
    {
        image.set(path);
        return true;
    } else
    {
        /* It might be a space delimited list */
        std::istringstream stream(path);
        std::string app_id;
        auto theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
        while (stream >> app_id)
        {
            if (custom_icons.count(app_id) > 0)
            {
                std::string next_path = custom_icons[app_id];
                if ((next_path.rfind("/", 0) == 0) || (next_path.rfind("~", 0) == 0))
                {
                    image.set(next_path);
                } else
                {
                    image.set_from_icon_name(next_path);
                }

                return true;
            }

            /* If the icon theme has this, use it */
            if (theme && theme->has_icon(app_id))
            {
                image.set_from_icon_name(app_id);
                return true;
            }

            /* Might be appid, get the .desktop file */
            auto icon = get_from_desktop_app_info(app_id);
            if (icon)
            {
                /* Handles both themed icons and icon files */
                image.set(icon);
                return true;
            }
        }
    }

    return false;
}

void load_custom_icons(std::string section_name)
{
    static const std::string prefix = "icon_mapping_";
    auto section = WayfireShellApp::get().config.get_section(section_name);

    for (auto option : section->get_registered_options())
    {
        if (option->get_name().compare(0, prefix.length(), prefix) != 0)
        {
            continue;
        }

        auto app_id = option->get_name().substr(prefix.length());
        custom_icons[app_id] = option->get_value_str();
    }
}
}
