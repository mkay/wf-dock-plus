#include <giomm/desktopappinfo.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <algorithm>
#include <iostream>

#include "launchers.hpp"
#include "gtk-utils.hpp"
#include "wf-shell-app.hpp"

static std::string get_dock_option(const std::string& name)
{
    auto section = WayfireShellApp::get().config.get_section("dock");
    auto option  = section ? section->get_option_or(name) : nullptr;
    return option ? option->get_value_str() : "";
}

/* image_set_icon() takes "~/..." as a file path but doesn't expand it */
static std::string expand_home(const std::string& path)
{
    if (path.rfind("~/", 0) == 0)
    {
        return Glib::get_home_dir() + path.substr(1);
    }

    return path;
}

bool WfDockLauncher::initialize(const std::string& name)
{
    std::string icon;
    Glib::RefPtr<Gio::AppInfo> app_info;
    auto cmd = get_dock_option("launcher_cmd_" + name);
    if (!cmd.empty())
    {
        auto label = get_dock_option("launcher_label_" + name);
        app_info = Gio::AppInfo::create_from_commandline(cmd,
            label.empty() ? name : label, Gio::AppInfo::CreateFlags::NONE);
        icon = expand_home(get_dock_option("launcher_icon_" + name));
        if (icon.empty())
        {
            icon = name;
        }

        add_app_id(name);
    } else
    {
        /* Accept "firefox", "firefox.desktop" and absolute paths to .desktop files */
        Glib::RefPtr<Gio::DesktopAppInfo> desktop_info;
        if (!name.empty() && (name[0] == '/'))
        {
            desktop_info = Gio::DesktopAppInfo::create_from_filename(name);
            add_app_id(Glib::path_get_basename(name));
        } else
        {
            desktop_info = Gio::DesktopAppInfo::create(name);
            if (!desktop_info)
            {
                desktop_info = Gio::DesktopAppInfo::create(name + ".desktop");
            }

            add_app_id(name);
        }

        if (desktop_info)
        {
            add_app_id(desktop_info->get_id());
            add_app_id(desktop_info->get_startup_wm_class());
        }

        app_info = desktop_info;
        if (app_info && app_info->get_icon())
        {
            icon = app_info->get_icon()->to_string();
        }
    }

    if (!app_info)
    {
        std::cerr << "wf-dock: no desktop entry or launcher_cmd_" << name <<
            " found for launcher \"" << name << "\"" << std::endl;
        return false;
    }

    /* For apps whose windows match neither the desktop id nor StartupWMClass */
    auto extra_ids = get_dock_option("launcher_app_id_" + name);
    for (auto& app_id : Glib::Regex::split_simple("\\s+", extra_ids.c_str()))
    {
        add_app_id(app_id);
    }

    IconProvider::image_set_icon(group.get_image(), icon);
    group.get_widget().add_css_class("launcher");
    group.set_app_info(app_info);
    return true;
}

/* Window app_ids are compared without case and without a .desktop suffix */
void WfDockLauncher::add_app_id(const std::string& app_id)
{
    std::string id = app_id;
    const std::string suffix = ".desktop";
    if ((id.size() > suffix.size()) &&
        (id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0))
    {
        id.resize(id.size() - suffix.size());
    }

    if (!id.empty())
    {
        app_ids.push_back(id);
    }
}

bool WfDockLauncher::matches(const std::string& app_id) const
{
    return !app_id.empty() && std::any_of(app_ids.begin(), app_ids.end(),
        [&] (const std::string& id)
    {
        return g_ascii_strcasecmp(id.c_str(), app_id.c_str()) == 0;
    });
}

WfDockLaunchers::WfDockLaunchers()
{
    box.add_css_class("launchers");
    reload();
}

Gtk::Box& WfDockLaunchers::get_widget()
{
    return box;
}

bool WfDockLaunchers::empty() const
{
    return launchers.empty();
}

void WfDockLaunchers::set_orientation(Gtk::Orientation orientation)
{
    box.set_orientation(orientation);
}

void WfDockLaunchers::reload()
{
    for (auto& launcher : launchers)
    {
        box.remove(launcher->group.get_widget());
    }

    launchers.clear();
    auto names = get_dock_option("launchers");
    for (auto& name : Glib::Regex::split_simple("\\s+", names.c_str()))
    {
        if (name.empty())
        {
            continue;
        }

        auto launcher = std::make_unique<WfDockLauncher>();
        if (launcher->initialize(name))
        {
            box.append(launcher->group.get_widget());
            launchers.push_back(std::move(launcher));
        }
    }
}

WfDockAppGroup*WfDockLaunchers::find(const std::string& app_id)
{
    for (auto& launcher : launchers)
    {
        if (launcher->matches(app_id))
        {
            return &launcher->group;
        }
    }

    return nullptr;
}
