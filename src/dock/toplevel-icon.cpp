#include "dock.hpp"
#include "app-group.hpp"
#include "toplevel-icon.hpp"

/* A window on one output. Instead of a button of its own, it reports to the
 * dock of that output, which groups the windows by app (see app-group.cpp). */
class WfToplevelIcon::impl
{
    zwlr_foreign_toplevel_handle_v1 *handle;
    wl_output *output;

    std::string app_id, title;
    uint32_t state = 0;
    /* WfToplevel sets the title, app_id and state right after creation; wait for
     * the app_id so the window doesn't briefly get an icon of its own */
    bool has_app_id = false;
    bool closing    = false;

    WfDockApps *get_apps()
    {
        auto dock = WfDockApp::get().dock_for_wl_output(output);
        return dock ? &dock->get_apps() : nullptr;
    }

    void sync()
    {
        auto apps = get_apps();
        if (apps && has_app_id && !closing)
        {
            apps->set_window(handle, app_id, title, state);
        }
    }

  public:
    impl(zwlr_foreign_toplevel_handle_v1 *handle, wl_output *output)
    {
        this->handle = handle;
        this->output = output;
    }

    void set_app_id(std::string app_id)
    {
        this->app_id = app_id;
        has_app_id   = true;
        sync();
    }

    void set_title(std::string title)
    {
        this->title = title;
        sync();
    }

    void set_state(uint32_t state)
    {
        this->state = state;
        sync();
    }

    void close()
    {
        closing = true;
        if (auto apps = get_apps())
        {
            apps->remove_window(handle, true);
        }
    }

    ~impl()
    {
        if (auto apps = get_apps())
        {
            apps->remove_window(handle, false);
        }
    }
};

WfToplevelIcon::WfToplevelIcon(zwlr_foreign_toplevel_handle_v1 *handle,
    wl_output *output) : pimpl(new impl(handle, output))
{}
WfToplevelIcon::~WfToplevelIcon() = default;

void WfToplevelIcon::set_title(std::string title)
{
    return pimpl->set_title(title);
}

void WfToplevelIcon::set_app_id(std::string app_id)
{
    return pimpl->set_app_id(app_id);
}

void WfToplevelIcon::set_state(uint32_t state)
{
    return pimpl->set_state(state);
}

void WfToplevelIcon::close()
{
    return pimpl->close();
}
