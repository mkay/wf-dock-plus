#pragma once

#include <string>

/* Opens a folder URI with dock/places_file_manager, or with the default
 * handler for folders when that is empty */
void open_in_file_manager(const std::string& uri);
