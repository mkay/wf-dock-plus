#pragma once

#include "glibmm/ustring.h"
#include <gtkmm/image.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/cssprovider.h>
#include <string>

/* Loads a CssProvider from the given path to the file, returns null if unsuccessful*/
Glib::RefPtr<Gtk::CssProvider> load_css_from_path(std::string path);

namespace IconProvider
{
bool image_set_icon(Gtk::Image & image, Glib::ustring path);
void load_custom_icons(std::string section_name);
}
