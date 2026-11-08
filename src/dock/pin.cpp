#include <glibmm/regex.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "pin.hpp"
#include "wf-shell-app.hpp"

static WfDockDragData *drag_data_copy(const WfDockDragData *data)
{
    auto copy = g_new(WfDockDragData, 1);
    copy->name   = g_strdup(data->name);
    copy->pinned = data->pinned;
    return copy;
}

static void drag_data_free(WfDockDragData *data)
{
    g_free(data->name);
    g_free(data);
}

G_DEFINE_BOXED_TYPE(WfDockDragData, wf_dock_drag_data, drag_data_copy, drag_data_free)

static std::string trim(const std::string& str)
{
    auto start = str.find_first_not_of(" \t\r");
    if (start == std::string::npos)
    {
        return "";
    }

    return str.substr(start, str.find_last_not_of(" \t\r") - start + 1);
}

/* Rewrites only the launchers line of the dock section, so options the dock
 * doesn't know and other programs' sections stay as they are. The file is
 * written in place: the config watch follows the file's inode, and the write
 * makes the dock reload the launchers. */
static void save(const std::vector<std::string>& names)
{
    std::string value;
    for (auto& name : names)
    {
        value += (value.empty() ? "" : " ") + name;
    }

    auto section = WayfireShellApp::get().config.get_section(DOCK_SECTION);
    if (auto option = section ? section->get_option_or("launchers") : nullptr)
    {
        option->set_value_str(value);
    }

    auto path = WayfireShellApp::get().get_config_file();
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            lines.push_back(line);
        }
    }

    const std::string header = "[" DOCK_SECTION "]";
    const std::string setting = "launchers = " + value;
    auto section_start = std::find_if(lines.begin(), lines.end(),
        [&] (const std::string& line) { return trim(line) == header; });
    if (section_start == lines.end())
    {
        if (!lines.empty() && !trim(lines.back()).empty())
        {
            lines.push_back("");
        }

        lines.push_back(header);
        lines.push_back(setting);
    } else
    {
        auto section_end = std::find_if(section_start + 1, lines.end(),
            [] (const std::string& line) { return trim(line).rfind("[", 0) == 0; });
        auto line = std::find_if(section_start + 1, section_end, [] (const std::string& line)
        {
            auto equals = line.find('=');
            return (equals != std::string::npos) && (trim(line.substr(0, equals)) == "launchers");
        });
        if (line == section_end)
        {
            lines.insert(section_start + 1, setting);
        } else
        {
            *line = setting;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    for (auto& line : lines)
    {
        out << line << '\n';
    }

    if (!out)
    {
        std::cerr << "wf-dock-plus: failed to save the launchers to " << path << std::endl;
    }
}

namespace launcher_config
{
std::vector<std::string> get()
{
    auto section = WayfireShellApp::get().config.get_section(DOCK_SECTION);
    auto option  = section ? section->get_option_or("launchers") : nullptr;
    std::vector<std::string> names;
    for (auto& name : Glib::Regex::split_simple("\\s+", option ? option->get_value_str().c_str() : ""))
    {
        if (!name.empty())
        {
            names.push_back(name);
        }
    }

    return names;
}

void pin(const std::string& name, const std::string& before)
{
    auto names = get();
    auto old   = std::find(names.begin(), names.end(), name);
    auto index = old - names.begin();
    if (old != names.end())
    {
        names.erase(old);
    }

    auto target = std::find(names.begin(), names.end(), before);
    if (before == name)
    {
        /* Dropped in front of itself: stays where it was */
        target = names.begin() + std::min<size_t>(index, names.size());
    }

    names.insert(target, name);
    save(names);
}

void unpin(const std::string& name)
{
    auto names = get();
    names.erase(std::remove(names.begin(), names.end(), name), names.end());
    save(names);
}

static std::function<void()> deferred;

void defer(std::function<void()> change)
{
    deferred = std::move(change);
}

void run_deferred()
{
    auto change = std::move(deferred);
    deferred = nullptr;
    if (change)
    {
        change();
    }
}
}
