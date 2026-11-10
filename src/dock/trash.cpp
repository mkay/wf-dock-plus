#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <giomm/memoryoutputstream.h>
#include <gdk/gdk.h>
#include <wf-autohide-window.hpp>
#include <wayfire/config/types.hpp>
#include <atomic>
#include <iostream>
#include <thread>

#include "trash.hpp"
#include "file-manager.hpp"
#include "gtk-utils.hpp"
#include "wf-shell-app.hpp"

#define TRASH_URI "trash:///"
#define ITEM_COUNT "trash::item-count"

/* All docks share one trash, so one emptying at a time across them */
static std::atomic<bool> emptying{false};

static std::string get_dock_option(const std::string& name)
{
    auto section = WayfireShellApp::get().config.get_section(DOCK_SECTION);
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

/* Like `gio trash --empty`: gvfs deletes files in trash:/// permanently, but
 * a folder only once it is empty */
static void delete_children(const Glib::RefPtr<Gio::File>& dir)
{
    try {
        auto children = dir->enumerate_children("standard::name,standard::type",
            Gio::FileQueryInfoFlags::NOFOLLOW_SYMLINKS);
        while (auto info = children->next_file())
        {
            auto child = dir->get_child(info->get_name());
            if (info->get_file_type() == Gio::FileType::DIRECTORY)
            {
                delete_children(child);
            }

            try {
                child->remove();
            } catch (const Glib::Error& e)
            {
                std::cerr << "wf-dock-plus: failed to delete " << child->get_uri() << ": " <<
                    e.what() << std::endl;
            }
        }
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-dock-plus: failed to list " << dir->get_uri() << ": " <<
            e.what() << std::endl;
    }
}

static void trash_file(const Glib::RefPtr<Gio::File>& file)
{
    file->trash_async([file] (Glib::RefPtr<Gio::AsyncResult>& result)
    {
        try {
            file->trash_finish(result);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock-plus: failed to trash " << file->get_uri() << ": " <<
                e.what() << std::endl;
        }
    });
}

/* Reads the dropped uri list and trashes every file in it */
static void trash_dropped(const Glib::RefPtr<Gdk::Drop>& drop, Glib::RefPtr<Gio::AsyncResult>& result)
{
    Glib::RefPtr<Gio::InputStream> input;
    try {
        Glib::ustring mime_type;
        input = drop->read_finish(result, mime_type);
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-dock-plus: failed to read the files dropped on the trash: " <<
            e.what() << std::endl;
        drop->finish(Gdk::DragAction::NONE);
        return;
    }

    auto output = Gio::MemoryOutputStream::create();
    output->splice_async(input, [drop, output] (Glib::RefPtr<Gio::AsyncResult>& result)
    {
        try {
            output->splice_finish(result);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock-plus: failed to read the files dropped on the trash: " <<
                e.what() << std::endl;
            drop->finish(Gdk::DragAction::NONE);
            return;
        }

        auto bytes = output->steal_as_bytes();
        gsize size = 0;
        auto data  = (const char*)bytes->get_data(size);
        std::string text(data ? data : "", size);
        gchar **uris = g_uri_list_extract_uris(text.c_str());
        for (gchar **uri = uris; uri && *uri; uri++)
        {
            trash_file(Gio::File::create_for_uri(*uri));
        }

        g_strfreev(uris);
        drop->finish(Gdk::DragAction::MOVE);
    }, Gio::OutputStream::SpliceFlags::CLOSE_SOURCE | Gio::OutputStream::SpliceFlags::CLOSE_TARGET);
}

WfDockTrash::WfDockTrash()
{
    button.set_child(image);
    button.set_has_frame(false);
    button.add_css_class("toplevel-icon");
    button.add_css_class("launcher");
    button.add_css_class("trash");
    button.set_focusable(false);
    button.signal_clicked().connect([this] () { open(); });

    list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    list.add_css_class("trash-list");
    list.signal_row_activated().connect([this] (Gtk::ListBoxRow *row)
    {
        activate(row->get_index());
    });
    popover.set_child(list);
    popover.add_css_class("trash-popover");
    popover.set_parent(button);
    popover.signal_show().connect([this] () { set_popover_open(true); });
    popover.signal_closed().connect([this] () { set_popover_open(false); });

    auto menu = Gtk::GestureClick::create();
    menu->set_button(GDK_BUTTON_SECONDARY);
    menu->signal_pressed().connect([this] (int, double, double)
    {
        confirming = false;
        rebuild_menu();
        popover.popup();
    });
    button.add_controller(menu);

    /* Files from a file manager, read as text/uri-list. A GdkFileList would
     * let GTK take them through the file transfer portal, whose copies in
     * /run/user/<uid>/doc can't be trashed. Dock launchers aren't uri lists, so
     * their drags go on to the dock, which unpins them here. */
    drop = Gtk::DropTargetAsync::create(Gdk::ContentFormats::create("text/uri-list"),
        Gdk::DragAction::MOVE | Gdk::DragAction::COPY);
    drop->signal_drop().connect([] (const Glib::RefPtr<Gdk::Drop>& drop, double, double)
    {
        drop->read_async({"text/uri-list"}, Glib::PRIORITY_DEFAULT,
            [drop] (Glib::RefPtr<Gio::AsyncResult>& result) { trash_dropped(drop, result); });
        return true;
    }, false);
    button.add_controller(drop);

    reload();
}

WfDockTrash::~WfDockTrash()
{
    count_timeout.disconnect();
    if (count_query)
    {
        count_query->cancel();
    }

    if (monitor)
    {
        monitor->cancel();
    }

    popover.unparent();
}

Gtk::Button& WfDockTrash::get_widget()
{
    return button;
}

bool WfDockTrash::visible() const
{
    return button.get_visible();
}

void WfDockTrash::set_dock_position(const std::string& position)
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

void WfDockTrash::reload()
{
    auto enabled = wf::option_type::from_string<bool>(get_dock_option("trash_button")).value_or(false);
    /* Tried again on every reload, in case gvfs has been started since */
    if (enabled && !available)
    {
        start_monitor();
    }

    button.set_visible(enabled && available);
    auto label = get_dock_option("trash_label");
    button.set_tooltip_text(label.empty() ? "Trash" : label);
    update_icon();
}

/* trash:/// covers the home trash and the trash folders of other drives. It
 * needs gvfs; without it GIO can still trash files but not list them. */
void WfDockTrash::start_monitor()
{
    trash = Gio::File::create_for_uri(TRASH_URI);
    try {
        auto info = trash->query_info(ITEM_COUNT);
        if (!info->has_attribute(ITEM_COUNT))
        {
            throw Gio::Error(Gio::Error::NOT_SUPPORTED, "no item count");
        }

        count = info->get_attribute_uint32(ITEM_COUNT);
        monitor = trash->monitor_directory();
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-dock-plus: hiding the trash button, " TRASH_URI " is unavailable (" <<
            e.what() << "); it needs gvfs" << std::endl;
        return;
    }

    monitor->signal_changed().connect([this] (const Glib::RefPtr<Gio::File>&,
                                              const Glib::RefPtr<Gio::File>&, Gio::FileMonitor::Event)
    {
        schedule_count_update();
    });
    available = true;
}

/* Emptying or trashing many files reports every one; count once they settle */
void WfDockTrash::schedule_count_update()
{
    count_timeout.disconnect();
    count_timeout = Glib::signal_timeout().connect([this] ()
    {
        update_count();
        return false;
    }, 200);
}

void WfDockTrash::update_count()
{
    if (count_query)
    {
        count_query->cancel();
    }

    /* The callback runs even when cancelled, possibly after this is gone, so
     * it checks its own cancellable before touching this */
    auto cancellable = Gio::Cancellable::create();
    auto file = trash;
    count_query = cancellable;
    file->query_info_async([this, file, cancellable] (Glib::RefPtr<Gio::AsyncResult>& result)
    {
        if (cancellable->is_cancelled())
        {
            return;
        }

        try {
            auto info = file->query_info_finish(result);
            count = info->get_attribute_uint32(ITEM_COUNT);
        } catch (const Glib::Error& e)
        {
            std::cerr << "wf-dock-plus: failed to count the trash: " << e.what() << std::endl;
            return;
        }

        update_icon();
        if (popover.get_visible() && !confirming)
        {
            rebuild_menu();
        }
    }, cancellable, ITEM_COUNT);
}

void WfDockTrash::update_icon()
{
    std::string icon;
    if (count > 0)
    {
        icon = get_dock_option("trash_icon_full");
        button.add_css_class("full");
    } else
    {
        icon = get_dock_option("trash_icon");
        button.remove_css_class("full");
    }

    if (icon.empty())
    {
        icon = (count > 0) ? "user-trash-full" : "user-trash";
    }

    IconProvider::image_set_icon(image, expand_home(icon));
}

static Gtk::ListBoxRow *add_row(Gtk::ListBox& list, const std::string& text, bool sensitive)
{
    auto label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0);
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*label);
    row->set_sensitive(sensitive);
    list.append(*row);
    return row;
}

/* Open and Empty; Empty first asks with the number of items */
void WfDockTrash::rebuild_menu()
{
    while (auto row = list.get_row_at_index(0))
    {
        list.remove(*row);
    }

    if (confirming)
    {
        auto question = add_row(list, "Permanently delete " + std::to_string(count) +
            (count == 1 ? " item?" : " items?"), true);
        question->get_child()->add_css_class("dim-label");
        question->set_activatable(false);
        question->set_selectable(false);
        add_row(list, "Empty Trash", true);
        add_row(list, "Cancel", true);
    } else
    {
        add_row(list, "Open Trash", true);
        add_row(list, emptying ? "Emptying…" : "Empty Trash", (count > 0) && !emptying);
    }

    /* For Enter and the arrow keys; the theme shows it only once they are used */
    auto first = list.get_row_at_index(confirming ? 1 : 0);
    list.select_row(*first);
    first->grab_focus();
}

void WfDockTrash::activate(int index)
{
    if (confirming)
    {
        popover.popdown();
        if (index == 1)
        {
            empty();
        }

        return;
    }

    if (index == 0)
    {
        popover.popdown();
        open();
    } else
    {
        confirming = true;
        rebuild_menu();
    }
}

void WfDockTrash::open()
{
    open_in_file_manager(TRASH_URI);
}

/* Off the main thread: emptying a large trash takes a while. The trash
 * monitor updates the icon as it goes. */
void WfDockTrash::empty()
{
    if (emptying.exchange(true))
    {
        return;
    }

    std::thread([] ()
    {
        delete_children(Gio::File::create_for_uri(TRASH_URI));
        emptying = false;
    }).detach();
}

/* Keeps the dock shown and lets it take keyboard input while the popover is open */
void WfDockTrash::set_popover_open(bool open)
{
    if (auto window = dynamic_cast<WayfireAutohidingWindow*>(button.get_root()))
    {
        window->set_popover_open(open);
    }
}
