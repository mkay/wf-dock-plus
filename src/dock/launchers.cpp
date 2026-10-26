#include <giomm/desktopappinfo.h>
#include <glibmm/miscutils.h>
#include <glibmm/main.h>
#include <glibmm/regex.h>
#include <gdkmm/display.h>
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
    } else
    {
        /* Accept "firefox", "firefox.desktop" and absolute paths to .desktop files */
        Glib::RefPtr<Gio::DesktopAppInfo> desktop_info;
        if (!name.empty() && (name[0] == '/'))
        {
            desktop_info = Gio::DesktopAppInfo::create_from_filename(name);
        } else
        {
            desktop_info = Gio::DesktopAppInfo::create(name);
            if (!desktop_info)
            {
                desktop_info = Gio::DesktopAppInfo::create(name + ".desktop");
            }
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

    IconProvider::image_set_icon(image, icon);
    button.set_child(image);
    button.set_tooltip_text(app_info->get_display_name());
    button.add_css_class("flat");
    button.add_css_class("toplevel-icon");
    button.add_css_class("launcher");
    button.set_focusable(false);
    button.signal_clicked().connect(sigc::mem_fun(*this, &WfDockLauncher::launch));
    return true;
}

/* Keep in sync with the launch-bounce animation in default.css (2 x 550ms) */
static constexpr int LAUNCH_BOUNCE_MS = 1100;

WfDockLauncher::~WfDockLauncher()
{
    launching_timeout.disconnect();
}

void WfDockLauncher::launch()
{
    auto ctx = Gdk::Display::get_default()->get_app_launch_context();
    try {
        app_info->launch(std::vector<Glib::RefPtr<Gio::File>>(), ctx);
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-dock: failed to launch " << app_info->get_display_name() <<
            ": " << e.what() << std::endl;
        return;
    }

    button.add_css_class("launching");
    launching_timeout.disconnect();
    launching_timeout = Glib::signal_timeout().connect([this] ()
    {
        button.remove_css_class("launching");
        return false;
    }, LAUNCH_BOUNCE_MS);
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
        box.remove(launcher->button);
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
            box.append(launcher->button);
            launchers.push_back(std::move(launcher));
        }
    }
}
