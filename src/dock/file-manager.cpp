#include <giomm/appinfo.h>
#include <gdkmm/display.h>
#include <iostream>

#include "file-manager.hpp"
#include "wf-shell-app.hpp"

void open_in_file_manager(const std::string& uri)
{
    auto ctx = Gdk::Display::get_default()->get_app_launch_context();

    auto section = WayfireShellApp::get().config.get_section(DOCK_SECTION);
    auto option  = section ? section->get_option_or("places_file_manager") : nullptr;
    auto cmd     = option ? option->get_value_str() : "";
    if (!cmd.empty())
    {
        /* GIO appends the folder URI to a command line without field codes */
        try {
            auto app = Gio::AppInfo::create_from_commandline(cmd, "",
                Gio::AppInfo::CreateFlags::SUPPORTS_URIS);
            app->launch_uri(uri, ctx);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock-plus: failed to run \"" << cmd << "\" for " << uri <<
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
            std::cerr << "wf-dock-plus: failed to open " << uri << ": " << e.what() << std::endl;
        }
    });
}
