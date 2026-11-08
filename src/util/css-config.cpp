#include <css-config.hpp>
#include <sstream>
#include <string>

void CssFromConfig::add_provider()
{
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void CssFromConfig::remove_provider()
{
    Gtk::StyleContext::remove_provider_for_display(Gdk::Display::get_default(), provider);
}

CssFromConfigInt::CssFromConfigInt(std::string option_name, std::string css_before, std::string css_after) :
    option_value{option_name}
{
    provider = Gtk::CssProvider::create();
    auto cb = [=]
    {
        // TODO When we go up to c++20 use std::format
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << css_before << std::to_string(option_value.value()) << css_after;
        provider->load_from_string(ss.str());
    };
    option_value.set_callback(cb);
    cb();
    add_provider();
}
